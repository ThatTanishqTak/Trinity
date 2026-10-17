#include "Trinity/Core/Platform.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Input/Input.hpp"
#include "Trinity/Platform/Windows/WindowsHeaders.hpp"

#if defined(TR_GAMEINPUT)
#include <GameInput.h>
#include <wrl/client.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <utility>

namespace Trinity
{
    namespace Platform
    {
#if defined(TR_GAMEINPUT)
        namespace
        {
            using Microsoft::WRL::ComPtr;
            using namespace GameInput::v3;

            // GameInput's flag for each GamepadButton, in GamepadButton's order
            constexpr std::array<GameInputGamepadButtons, std::to_underlying(GamepadButton::Count)> c_Buttons
            {
                GameInputGamepadA, GameInputGamepadB, GameInputGamepadX, GameInputGamepadY,
                GameInputGamepadLeftShoulder, GameInputGamepadRightShoulder,
                GameInputGamepadView, GameInputGamepadMenu,
                GameInputGamepadLeftThumbstick, GameInputGamepadRightThumbstick,
                GameInputGamepadDPadUp, GameInputGamepadDPadDown, GameInputGamepadDPadLeft, GameInputGamepadDPadRight
            };

            // More pads can be connected than there are slots, and those without one wait until a slot is free
            constexpr std::size_t c_DeviceCapacity = 16;

            // The pad in a slot, the reading it was last read up to and the rumble it was last given. The ID is of the pad that had the slot last, so the same pad gets it back
            struct GamepadSlot
            {
                ComPtr<IGameInputDevice> Device;
                ComPtr<IGameInputReading> Reading;
                APP_LOCAL_DEVICE_ID LastId{};
                bool HasLastId = false;
                float RumbleLow = 0.0f;
                float RumbleHigh = 0.0f;
            };

            struct GamepadState
            {
                ComPtr<IGameInput> Api;
                GameInputCallbackToken Callback = 0;
                std::array<GamepadSlot, Input::c_MaxGamepads> Slots;

                // The pads connected now, as GameInput's callback keeps them from a thread of its own
                std::mutex Mutex;
                std::array<ComPtr<IGameInputDevice>, c_DeviceCapacity> Connected;
                bool Overflowed = false;
            };

            GamepadState s_State;

            // Also called for each pad already connected as the callback is registered. Changes to a pad's other status flags are left out
            void CALLBACK OnDeviceStatus(GameInputCallbackToken, void*, IGameInputDevice* device, std::uint64_t, GameInputDeviceStatus currentStatus, GameInputDeviceStatus previousStatus)
            {
                const bool l_Connected = (currentStatus & GameInputDeviceConnected) != 0;
                if (l_Connected == ((previousStatus & GameInputDeviceConnected) != 0))
                {
                    return;
                }

                const std::scoped_lock l_Lock(s_State.Mutex);
                const auto a_Known = std::ranges::find_if(s_State.Connected, [device](const ComPtr<IGameInputDevice>& it_Device) { return it_Device.Get() == device; });
                if (!l_Connected)
                {
                    if (a_Known != s_State.Connected.end())
                    {
                        a_Known->Reset();
                    }

                    return;
                }

                if (a_Known != s_State.Connected.end())
                {
                    return;
                }

                const auto a_Free = std::ranges::find_if(s_State.Connected, [](const ComPtr<IGameInputDevice>& it_Device) { return !it_Device; });
                if (a_Free != s_State.Connected.end())
                {
                    *a_Free = device;
                }
                else if (!std::exchange(s_State.Overflowed, true))
                {
                    TR_CORE_WARN("More than {} gamepads are connected, so the rest are left out", c_DeviceCapacity);
                }
            }

            // The slot this pad had last, or else one no pad has had yet, or else the first one free
            std::optional<std::uint32_t> ChooseSlot(const APP_LOCAL_DEVICE_ID& id)
            {
                std::optional<std::uint32_t> l_Unused;
                std::optional<std::uint32_t> l_Free;
                for (std::uint32_t it_Slot = 0; it_Slot < Input::c_MaxGamepads; ++it_Slot)
                {
                    const GamepadSlot& l_Slot = s_State.Slots[it_Slot];
                    if (l_Slot.Device)
                    {
                        continue;
                    }

                    if (l_Slot.HasLastId && std::memcmp(&l_Slot.LastId, &id, sizeof(id)) == 0)
                    {
                        return it_Slot;
                    }

                    if (!l_Slot.HasLastId && !l_Unused)
                    {
                        l_Unused = it_Slot;
                    }

                    if (!l_Free)
                    {
                        l_Free = it_Slot;
                    }
                }

                return l_Unused ? l_Unused : l_Free;
            }

            void Report(GamepadListener& listener, std::uint32_t slot, IGameInputReading& reading)
            {
                GameInputGamepadState l_State{};
                if (!reading.GetGamepadState(&l_State))
                {
                    return;
                }

                GamepadReading l_Reading;
                for (std::size_t it_Button = 0; it_Button < c_Buttons.size(); ++it_Button)
                {
                    if ((l_State.buttons & c_Buttons[it_Button]) != 0)
                    {
                        l_Reading.Buttons |= 1u << it_Button;
                    }
                }

                l_Reading.Axes = { l_State.leftThumbstickX, l_State.leftThumbstickY, l_State.rightThumbstickX, l_State.rightThumbstickY, l_State.leftTrigger, l_State.rightTrigger };
                listener.OnGamepadReading(slot, l_Reading);
            }

            // Every reading after the one last read up to. Without one, or once GameInput no longer keeps the readings after it, the reading now alone
            void ReadSlot(GamepadListener& listener, std::uint32_t slot, GamepadSlot& state)
            {
                while (state.Reading)
                {
                    ComPtr<IGameInputReading> l_Next;
                    const HRESULT l_Result = s_State.Api->GetNextReading(state.Reading.Get(), GameInputKindGamepad, state.Device.Get(), &l_Next);
                    if (FAILED(l_Result))
                    {
                        if (l_Result != GAMEINPUT_E_REFERENCE_READING_TOO_OLD)
                        {
                            return;
                        }

                        state.Reading.Reset();

                        break;
                    }

                    Report(listener, slot, *l_Next.Get());
                    state.Reading = std::move(l_Next);
                }

                ComPtr<IGameInputReading> l_Current;
                if (SUCCEEDED(s_State.Api->GetCurrentReading(GameInputKindGamepad, state.Device.Get(), &l_Current)))
                {
                    Report(listener, slot, *l_Current.Get());
                    state.Reading = std::move(l_Current);
                }
            }

            void StopRumble(GamepadSlot& slot)
            {
                if (slot.Device && (slot.RumbleLow != 0.0f || slot.RumbleHigh != 0.0f))
                {
                    const GameInputRumbleParams l_Stopped{};
                    slot.Device->SetRumbleState(&l_Stopped);
                }

                slot.RumbleLow = 0.0f;
                slot.RumbleHigh = 0.0f;
            }
        }

        // GameInput.lib finds the newest runtime installed, the GameInput redistributable or the one Windows has, and fails when neither is there
        void InitializeGamepads()
        {
            const HRESULT l_Result = GameInputCreate(&s_State.Api);
            if (FAILED(l_Result))
            {
                s_State.Api.Reset();
                TR_CORE_WARN("GameInput is not installed (0x{:08X}), so there are no gamepads. The GameInput redistributable installs it", static_cast<std::uint32_t>(l_Result));

                return;
            }

            // Pads already connected are reported before this returns
            const HRESULT l_Registered = s_State.Api->RegisterDeviceCallback(nullptr, GameInputKindGamepad, GameInputDeviceConnected, GameInputBlockingEnumeration, nullptr, OnDeviceStatus, &s_State.Callback);
            if (FAILED(l_Registered))
            {
                s_State.Api.Reset();
                TR_CORE_WARN("GameInput could not report gamepads (0x{:08X}), so there are none", static_cast<std::uint32_t>(l_Registered));
            }
        }

        // Unregistering waits for a callback already running, so nothing touches the pads after
        void ShutdownGamepads()
        {
            if (!s_State.Api)
            {
                return;
            }

            s_State.Api->UnregisterCallback(s_State.Callback);
            s_State.Callback = 0;

            for (GamepadSlot& it_Slot : s_State.Slots)
            {
                StopRumble(it_Slot);
                it_Slot = {};
            }

            {
                const std::scoped_lock l_Lock(s_State.Mutex);
                for (ComPtr<IGameInputDevice>& it_Device : s_State.Connected)
                {
                    it_Device.Reset();
                }

                s_State.Overflowed = false;
            }

            s_State.Api.Reset();
        }

        void PollGamepads(GamepadListener& listener, bool resync)
        {
            if (!s_State.Api)
            {
                return;
            }

            std::array<ComPtr<IGameInputDevice>, c_DeviceCapacity> l_Connected;
            {
                const std::scoped_lock l_Lock(s_State.Mutex);
                l_Connected = s_State.Connected;
            }

            const auto a_IsConnected = [&l_Connected](const IGameInputDevice* device)
            {
                return std::ranges::any_of(l_Connected, [device](const ComPtr<IGameInputDevice>& it_Device) { return it_Device.Get() == device; });
            };

            for (std::uint32_t it_Slot = 0; it_Slot < Input::c_MaxGamepads; ++it_Slot)
            {
                GamepadSlot& l_Slot = s_State.Slots[it_Slot];
                if (l_Slot.Device && !a_IsConnected(l_Slot.Device.Get()))
                {
                    l_Slot.Device.Reset();
                    l_Slot.Reading.Reset();
                    l_Slot.RumbleLow = 0.0f;
                    l_Slot.RumbleHigh = 0.0f;
                    listener.OnGamepadDisconnected(it_Slot);
                }
            }

            for (const ComPtr<IGameInputDevice>& it_Device : l_Connected)
            {
                if (!it_Device || std::ranges::any_of(s_State.Slots, [&it_Device](const GamepadSlot& it_Slot) { return it_Slot.Device.Get() == it_Device.Get(); }))
                {
                    continue;
                }

                const GameInputDeviceInfo* l_Info = nullptr;
                if (FAILED(it_Device->GetDeviceInfo(&l_Info)) || l_Info == nullptr)
                {
                    continue;
                }

                const std::optional<std::uint32_t> l_Slot = ChooseSlot(l_Info->deviceId);
                if (!l_Slot)
                {
                    continue;
                }

                GamepadSlot& l_Chosen = s_State.Slots[*l_Slot];
                l_Chosen.Device = it_Device;
                l_Chosen.LastId = l_Info->deviceId;
                l_Chosen.HasLastId = true;
                listener.OnGamepadConnected(*l_Slot, l_Info->displayName != nullptr ? l_Info->displayName : "Gamepad");
            }

            for (std::uint32_t it_Slot = 0; it_Slot < Input::c_MaxGamepads; ++it_Slot)
            {
                GamepadSlot& l_Slot = s_State.Slots[it_Slot];
                if (!l_Slot.Device)
                {
                    continue;
                }

                if (resync)
                {
                    l_Slot.Reading.Reset();
                }

                ReadSlot(listener, it_Slot, l_Slot);
            }
        }

        void SetGamepadRumble(std::uint32_t slot, float lowFrequency, float highFrequency)
        {
            if (slot >= Input::c_MaxGamepads)
            {
                return;
            }

            GamepadSlot& l_Slot = s_State.Slots[slot];
            if (!l_Slot.Device || (l_Slot.RumbleLow == lowFrequency && l_Slot.RumbleHigh == highFrequency))
            {
                return;
            }

            l_Slot.RumbleLow = lowFrequency;
            l_Slot.RumbleHigh = highFrequency;

            const GameInputRumbleParams l_Params{ lowFrequency, highFrequency, 0.0f, 0.0f };
            l_Slot.Device->SetRumbleState(&l_Params);
        }
#else
        // Built without the GameInput package, which Scripts/Bootstrap.ps1 downloads
        void InitializeGamepads()
        {
            TR_CORE_WARN("This build has no GameInput, so there are no gamepads. Run Scripts/Bootstrap.ps1 and build again to add them");
        }

        void ShutdownGamepads()
        {

        }

        void PollGamepads(GamepadListener&, bool)
        {

        }

        void SetGamepadRumble(std::uint32_t, float, float)
        {

        }
#endif
    }
}