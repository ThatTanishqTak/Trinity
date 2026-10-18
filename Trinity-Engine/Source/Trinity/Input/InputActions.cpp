#include "Trinity/Input/InputActions.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Input/InputActionData.hpp"
#include "Trinity/Input/InputActionsRuntime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <utility>

namespace Trinity
{
    namespace
    {
        constexpr std::uint32_t c_NoAction = std::numeric_limits<std::uint32_t>::max();
        constexpr std::size_t c_DeviceCount = std::to_underlying(InputDevice::Count);

        // A path a handle was found by, and the action it leads to in the asset now, by its index among every map's actions in order
        struct ActionSlot
        {
            InputName Path;
            std::uint32_t Action = c_NoAction;
        };

        // Where each action is in the asset, and its type, kept here so reading an action never looks into an asset a reload may just have replaced
        struct ActionLocation
        {
            std::uint32_t Map = 0;
            std::uint32_t Action = 0;
            InputActionType Type = InputActionType::Button;
        };

        // A map enabled or disabled before the asset was ready, made once it is, after the first map
        struct MapChange
        {
            InputName Map;
            bool Enable = true;
        };

        // One player's reading of one action: its value, whether it is held, whether it went down or up this frame, and the same since the last fixed step
        struct ActionState
        {
            glm::vec2 Value{ 0.0f };
            bool Held = false;
            bool Pressed = false;
            bool Released = false;
            bool FixedPressed = false;
            bool FixedReleased = false;
        };

        // The maps a player enabled, by name so they outlive a reload, and by index into the asset now. A new player has the first map enabled once the asset is there, then the changes asked for until it was
        struct PlayerState
        {
            bool Active = false;
            bool FirstMapPending = false;
            InputVector<InputName> EnabledNames;
            InputVector<MapChange> PendingChanges;
            InputVector<std::uint8_t> EnabledMaps;
            InputVector<ActionState> Actions;
        };

        struct State
        {
            InputActionsLoader Loader;
            UUID Asset;
            bool Running = false;
            bool InFixedStep = false;
            bool WarnedType = false;
            std::uint64_t BuiltSerial = 0;
            // The version built from, null until it is ready
            const InputActionsData* Data = nullptr;
            InputVector<ActionLocation> Locations;
            InputVector<ActionSlot> Slots;
            std::array<PlayerState, InputActions::c_MaxPlayers> Players;
            // Each device's assigned player, or c_NoPlayer, and the devices each player reads this frame, bit 1 << InputDevice
            std::array<std::uint32_t, c_DeviceCount> DevicePlayers{};
            std::array<std::uint32_t, InputActions::c_MaxPlayers> ReadMasks{};
            // Counts up as a scene starts and ends, so a handle found in an earlier one reads nothing
            std::uint32_t Session = 1;
        };

        State* s_State = nullptr;

        // A source's value, its X alone for one that has no Y, and whether a key or button of it went down this frame, which a press and a release between two frames would otherwise hide
        struct Reading
        {
            glm::vec2 Value{ 0.0f };
            bool IsVector = false;
            bool WentDown = false;
        };

        [[nodiscard]] bool IsRunning()
        {
            return s_State != nullptr && s_State->Running;
        }

        [[nodiscard]] bool IsActive(std::uint32_t player)
        {
            return IsRunning() && player < InputActions::c_MaxPlayers && s_State->Players[player].Active;
        }

        [[nodiscard]] float GetMagnitude(const Reading& reading)
        {
            return reading.IsVector ? glm::length(reading.Value) : std::abs(reading.Value.x);
        }

        // The vector's X or Y alone, by the code of a mouse source: 0 for both, 1 for X and 2 for Y
        [[nodiscard]] Reading Pick(glm::vec2 value, std::uint16_t code)
        {
            Reading l_Reading;
            l_Reading.IsVector = code == 0;
            l_Reading.Value = code == 2 ? glm::vec2(value.y, 0.0f) : code == 1 ? glm::vec2(value.x, 0.0f) : value;

            return l_Reading;
        }

        // The strongest of the player's gamepads, so player 0 reads every pad it has as one
        template<typename Read>
        [[nodiscard]] Reading ReadGamepads(std::uint32_t mask, const Read& read)
        {
            Reading l_Best;
            for (std::uint32_t it_Gamepad = 0; it_Gamepad < Input::c_MaxGamepads; ++it_Gamepad)
            {
                if ((mask & (1u << std::to_underlying(GetGamepadDevice(it_Gamepad)))) == 0 || !Input::IsGamepadConnected(it_Gamepad))
                {
                    continue;
                }

                const Reading l_Reading = read(it_Gamepad);
                l_Best.WentDown = l_Best.WentDown || l_Reading.WentDown;
                l_Best.IsVector = l_Reading.IsVector;
                if (GetMagnitude(l_Reading) > GetMagnitude(l_Best))
                {
                    l_Best.Value = l_Reading.Value;
                }
            }

            return l_Best;
        }

        [[nodiscard]] Reading ReadSource(InputSource source, std::uint32_t mask)
        {
            const bool l_KeyboardMouse = (mask & (1u << std::to_underlying(InputDevice::KeyboardMouse))) != 0;
            Reading l_Reading;
            switch (source.Kind)
            {
                case InputSourceKind::None:
                {
                    break;
                }
                case InputSourceKind::Key:
                {
                    if (l_KeyboardMouse)
                    {
                        const KeyCode l_Key = static_cast<KeyCode>(source.Code);
                        l_Reading.Value.x = Input::IsKeyPressed(l_Key) ? 1.0f : 0.0f;
                        l_Reading.WentDown = Input::WasKeyPressed(l_Key);
                    }

                    break;
                }
                case InputSourceKind::MouseButton:
                {
                    if (l_KeyboardMouse)
                    {
                        const MouseCode l_Button = static_cast<MouseCode>(source.Code);
                        l_Reading.Value.x = Input::IsMouseButtonPressed(l_Button) ? 1.0f : 0.0f;
                        l_Reading.WentDown = Input::WasMouseButtonPressed(l_Button);
                    }

                    break;
                }
                case InputSourceKind::MouseDelta:
                {
                    l_Reading = Pick(l_KeyboardMouse ? Input::GetMouseDelta() : glm::vec2(0.0f), source.Code);

                    break;
                }
                case InputSourceKind::MouseScroll:
                {
                    l_Reading = Pick(l_KeyboardMouse ? Input::GetMouseScroll() : glm::vec2(0.0f), source.Code);

                    break;
                }
                case InputSourceKind::GamepadButton:
                {
                    l_Reading = ReadGamepads(mask, [source](std::uint32_t gamepad)
                    {
                        const GamepadButton l_Button = static_cast<GamepadButton>(source.Code);

                        return Reading{ glm::vec2(Input::IsGamepadButtonPressed(gamepad, l_Button) ? 1.0f : 0.0f, 0.0f), false, Input::WasGamepadButtonPressed(gamepad, l_Button) };
                    });

                    break;
                }
                case InputSourceKind::GamepadAxis:
                {
                    l_Reading = ReadGamepads(mask, [source](std::uint32_t gamepad)
                    {
                        return Reading{ glm::vec2(Input::GetGamepadAxis(gamepad, static_cast<GamepadAxis>(source.Code)), 0.0f), false, false };
                    });

                    break;
                }
                case InputSourceKind::GamepadStick:
                {
                    l_Reading = ReadGamepads(mask, [source](std::uint32_t gamepad)
                    {
                        const GamepadAxis l_X = source.Code == 0 ? GamepadAxis::TR_LEFT_STICK_X : GamepadAxis::TR_RIGHT_STICK_X;
                        const GamepadAxis l_Y = source.Code == 0 ? GamepadAxis::TR_LEFT_STICK_Y : GamepadAxis::TR_RIGHT_STICK_Y;

                        return Reading{ glm::vec2(Input::GetGamepadAxis(gamepad, l_X), Input::GetGamepadAxis(gamepad, l_Y)), true, false };
                    });
                    l_Reading.IsVector = true;

                    break;
                }
            }

            return l_Reading;
        }

        // Nothing below min, all of it from max, and in between in proportion
        [[nodiscard]] float ApplyDeadzone(float size, float min, float max)
        {
            if (size < min)
            {
                return 0.0f;
            }

            return max > min ? std::min((size - min) / (max - min), 1.0f) : 1.0f;
        }

        void ApplyProcessor(const InputProcessor& processor, Reading& reading)
        {
            glm::vec2& l_Value = reading.Value;
            switch (processor.Kind)
            {
                case InputProcessorKind::RadialDeadzone:
                {
                    const float l_Length = GetMagnitude(reading);
                    l_Value = l_Length > 0.0f ? l_Value * (ApplyDeadzone(l_Length, processor.Min, processor.Max) / l_Length) : glm::vec2(0.0f);

                    break;
                }
                case InputProcessorKind::AxialDeadzone:
                {
                    l_Value.x = std::copysign(ApplyDeadzone(std::abs(l_Value.x), processor.Min, processor.Max), l_Value.x);
                    l_Value.y = std::copysign(ApplyDeadzone(std::abs(l_Value.y), processor.Min, processor.Max), l_Value.y);

                    break;
                }
                case InputProcessorKind::Invert:
                {
                    l_Value.x = processor.InvertX ? -l_Value.x : l_Value.x;
                    l_Value.y = processor.InvertY ? -l_Value.y : l_Value.y;

                    break;
                }
                case InputProcessorKind::Scale:
                {
                    l_Value *= processor.Scale;

                    break;
                }
                case InputProcessorKind::Normalize:
                {
                    const float l_Length = GetMagnitude(reading);
                    l_Value = l_Length > 1.0f ? l_Value / l_Length : l_Value;

                    break;
                }
            }
        }

        // A composite's part counts by its size if it is a stick, and by its value otherwise, so a trigger can be half of an axis
        [[nodiscard]] float ReadPart(InputSource source, std::uint32_t mask, bool& wentDown)
        {
            const Reading l_Reading = ReadSource(source, mask);
            wentDown = wentDown || l_Reading.WentDown;

            return l_Reading.IsVector ? glm::length(l_Reading.Value) : l_Reading.Value.x;
        }

        [[nodiscard]] Reading ReadBinding(const InputBinding& binding, std::uint32_t mask)
        {
            Reading l_Reading;
            switch (binding.Composite)
            {
                case InputCompositeKind::None:
                {
                    l_Reading = ReadSource(binding.Sources[0], mask);

                    break;
                }
                case InputCompositeKind::Axis:
                {
                    const float l_Negative = ReadPart(binding.Sources[0], mask, l_Reading.WentDown);
                    const float l_Positive = ReadPart(binding.Sources[1], mask, l_Reading.WentDown);
                    l_Reading.Value.x = l_Positive - l_Negative;

                    break;
                }
                case InputCompositeKind::Vector2:
                {
                    const float l_Up = ReadPart(binding.Sources[0], mask, l_Reading.WentDown);
                    const float l_Down = ReadPart(binding.Sources[1], mask, l_Reading.WentDown);
                    const float l_Left = ReadPart(binding.Sources[2], mask, l_Reading.WentDown);
                    const float l_Right = ReadPart(binding.Sources[3], mask, l_Reading.WentDown);
                    l_Reading.Value = glm::vec2(l_Right - l_Left, l_Up - l_Down);
                    l_Reading.IsVector = true;

                    break;
                }
            }

            for (const InputProcessor& it_Processor : binding.Processors)
            {
                ApplyProcessor(it_Processor, l_Reading);
            }

            return l_Reading;
        }

        // The binding's value as the action's type has it: a Button's strength, an Axis's X, or a Vector2, with how far it goes
        [[nodiscard]] glm::vec2 ToActionValue(InputActionType type, const Reading& reading, float& magnitude)
        {
            switch (type)
            {
                case InputActionType::Button:
                {
                    magnitude = GetMagnitude(reading);

                    return glm::vec2(magnitude, 0.0f);
                }
                case InputActionType::Axis:
                {
                    magnitude = std::abs(reading.Value.x);

                    return glm::vec2(reading.Value.x, 0.0f);
                }
                case InputActionType::Vector2:
                {
                    const glm::vec2 l_Value = reading.IsVector ? reading.Value : glm::vec2(reading.Value.x, 0.0f);
                    magnitude = glm::length(l_Value);

                    return l_Value;
                }
            }

            magnitude = 0.0f;

            return glm::vec2(0.0f);
        }

        // Of several bindings the strongest wins, the first among equals. An action is pressed as it goes from below the press point to it, and also when a key or button of it went down this frame and came up again before it ended
        void Evaluate(const InputActionDefinition& action, bool enabled, std::uint32_t mask, ActionState& state)
        {
            glm::vec2 l_Value(0.0f);
            float l_Magnitude = 0.0f;
            bool l_WentDown = false;
            if (enabled)
            {
                for (const InputBinding& it_Binding : action.Bindings)
                {
                    const Reading l_Reading = ReadBinding(it_Binding, mask);
                    float l_BindingMagnitude = 0.0f;
                    const glm::vec2 l_BindingValue = ToActionValue(action.Type, l_Reading, l_BindingMagnitude);
                    l_WentDown = l_WentDown || l_Reading.WentDown;
                    if (l_BindingMagnitude > l_Magnitude)
                    {
                        l_Magnitude = l_BindingMagnitude;
                        l_Value = l_BindingValue;
                    }
                }
            }

            const bool l_WasHeld = state.Held;
            state.Value = l_Value;
            state.Held = l_Magnitude >= InputActions::c_PressPoint;
            state.Pressed = !l_WasHeld && (state.Held || l_WentDown);
            state.Released = (l_WasHeld || state.Pressed) && !state.Held;
            state.FixedPressed = state.FixedPressed || state.Pressed;
            state.FixedReleased = state.FixedReleased || state.Released;
        }

        [[nodiscard]] std::uint32_t FindMap(const InputActionsData& data, std::string_view name)
        {
            const auto a_Found = std::ranges::find_if(data.Maps, [name](const InputActionMap& map) { return std::string_view(map.Name) == name; });

            return a_Found != data.Maps.end() ? static_cast<std::uint32_t>(a_Found - data.Maps.begin()) : c_NoAction;
        }

        // Map/Action, or an action's name alone where only one map has it. Logged when it leads nowhere
        [[nodiscard]] std::uint32_t Resolve(std::string_view path)
        {
            const InputActionsData* l_Data = s_State->Data;
            if (l_Data == nullptr)
            {
                return c_NoAction;
            }

            const std::size_t l_Separator = path.find(InputActionsData::c_PathSeparator);
            const std::string_view l_Map = l_Separator != std::string_view::npos ? path.substr(0, l_Separator) : std::string_view();
            const std::string_view l_Action = l_Separator != std::string_view::npos ? path.substr(l_Separator + 1) : path;
            std::uint32_t l_Found = c_NoAction;
            std::uint32_t l_Matches = 0;
            for (std::uint32_t it_Index = 0; it_Index < s_State->Locations.size(); ++it_Index)
            {
                const ActionLocation& l_Location = s_State->Locations[it_Index];
                const InputActionMap& l_ActionMap = l_Data->Maps[l_Location.Map];
                if ((l_Separator == std::string_view::npos || std::string_view(l_ActionMap.Name) == l_Map) && std::string_view(l_ActionMap.Actions[l_Location.Action].Name) == l_Action)
                {
                    l_Found = it_Index;
                    ++l_Matches;
                }
            }

            if (l_Matches > 1)
            {
                TR_CORE_WARN("Input: {} actions are named '{}', so it reads as released until it is found as Map/{}", l_Matches, path, path);

                return c_NoAction;
            }

            if (l_Matches == 0)
            {
                TR_CORE_WARN("Input: the actions asset has no action '{}', so it reads as released until it does", path);
            }

            return l_Found;
        }

        [[nodiscard]] bool HasName(const InputVector<InputName>& names, std::string_view name)
        {
            return std::ranges::any_of(names, [name](const InputName& it_Name) { return std::string_view(it_Name) == name; });
        }

        void SetEnabledName(PlayerState& player, std::string_view map, bool enable)
        {
            if (!enable)
            {
                std::erase_if(player.EnabledNames, [map](const InputName& name) { return std::string_view(name) == map; });
            }
            else if (!HasName(player.EnabledNames, map))
            {
                player.EnabledNames.push_back(InputName(map));
            }
        }

        void ApplyFirstMap(PlayerState& player)
        {
            if (s_State->Data == nullptr)
            {
                return;
            }

            if (std::exchange(player.FirstMapPending, false) && !s_State->Data->Maps.empty())
            {
                SetEnabledName(player, s_State->Data->Maps.front().Name, true);
            }

            for (const MapChange& it_Change : player.PendingChanges)
            {
                if (FindMap(*s_State->Data, it_Change.Map) == c_NoAction)
                {
                    TR_CORE_WARN("Input: map '{}' was not {}, since the actions asset has no map of that name", std::string_view(it_Change.Map), it_Change.Enable ? "enabled" : "disabled");

                    continue;
                }

                SetEnabledName(player, it_Change.Map, it_Change.Enable);
            }

            player.PendingChanges = InputVector<MapChange>();
        }

        // Sized for the asset now, with each enabled name's map, so reading allocates nothing until the next build. A name the asset lacks is kept, and logged
        void Fit(PlayerState& player)
        {
            ApplyFirstMap(player);

            const InputActionsData* l_Data = s_State->Data;
            const std::size_t l_Maps = l_Data != nullptr ? l_Data->Maps.size() : 0;
            player.EnabledMaps.assign(l_Maps, 0);
            player.Actions.assign(s_State->Locations.size(), ActionState{});
            for (const InputName& it_Name : player.EnabledNames)
            {
                const std::uint32_t l_Map = l_Data != nullptr ? FindMap(*l_Data, it_Name) : c_NoAction;
                if (l_Map != c_NoAction)
                {
                    player.EnabledMaps[l_Map] = 1;
                }
                else if (l_Data != nullptr)
                {
                    TR_CORE_WARN("Input: the actions asset has no map '{}', which stays enabled in case it comes back", std::string_view(it_Name));
                }
            }
        }

        // As the asset becomes ready and after each reload. Every action reads as released until the next frame's update
        void Build(const InputActionsAsset* asset)
        {
            s_State->Data = asset != nullptr ? &asset->GetData() : nullptr;
            s_State->BuiltSerial = asset != nullptr ? asset->GetSerial() : 0;
            s_State->Locations.clear();
            if (s_State->Data != nullptr)
            {
                for (std::uint32_t it_Map = 0; it_Map < s_State->Data->Maps.size(); ++it_Map)
                {
                    for (std::uint32_t it_Action = 0; it_Action < s_State->Data->Maps[it_Map].Actions.size(); ++it_Action)
                    {
                        s_State->Locations.push_back({ it_Map, it_Action, s_State->Data->Maps[it_Map].Actions[it_Action].Type });
                    }
                }
            }

            for (PlayerState& it_Player : s_State->Players)
            {
                if (it_Player.Active)
                {
                    Fit(it_Player);
                }
            }

            for (ActionSlot& it_Slot : s_State->Slots)
            {
                it_Slot.Action = Resolve(it_Slot.Path);
            }
        }

        // The asset as it is now, built from again when a reload has replaced it, so nothing reads the version it replaced
        const InputActionsData* Sync()
        {
            if (!IsRunning() || !s_State->Asset)
            {
                return nullptr;
            }

            const Asset* l_Loaded = AssetManager::GetAsset(s_State->Asset);
            const InputActionsAsset* l_Asset = l_Loaded != nullptr && l_Loaded->GetAssetType() == InputActionsAsset::c_AssetType ? static_cast<const InputActionsAsset*>(l_Loaded) : nullptr;
            if (l_Loaded != nullptr && l_Asset == nullptr && !std::exchange(s_State->WarnedType, true))
            {
                TR_CORE_ERROR("Input: asset {} is a {} asset, not input actions, so every action reads as released", s_State->Asset, l_Loaded->GetAssetType());
            }

            if ((l_Asset != nullptr ? l_Asset->GetSerial() : 0) != s_State->BuiltSerial)
            {
                Build(l_Asset);
            }

            return s_State->Data;
        }

        // Each player reads its assigned devices, and player 0 every one no player that is in use has
        void UpdateReadMasks()
        {
            s_State->ReadMasks.fill(0);
            for (std::size_t it_Device = 0; it_Device < c_DeviceCount; ++it_Device)
            {
                const std::uint32_t l_Assigned = s_State->DevicePlayers[it_Device];
                const std::uint32_t l_Player = IsActive(l_Assigned) ? l_Assigned : 0;
                s_State->ReadMasks[l_Player] |= 1u << it_Device;
            }
        }

        [[nodiscard]] const ActionState* FindState(std::uint32_t slot, std::uint32_t session, std::uint32_t player)
        {
            if (!IsActive(player) || session != s_State->Session || slot >= s_State->Slots.size())
            {
                return nullptr;
            }

            const std::uint32_t l_Action = s_State->Slots[slot].Action;
            const PlayerState& l_Player = s_State->Players[player];

            return l_Action < l_Player.Actions.size() ? &l_Player.Actions[l_Action] : nullptr;
        }

        void ResetPlayers()
        {
            for (PlayerState& it_Player : s_State->Players)
            {
                it_Player = PlayerState{};
            }

            s_State->DevicePlayers.fill(InputActions::c_NoPlayer);
        }

        [[nodiscard]] bool AnyGamepadButtonPressed(std::uint32_t gamepad)
        {
            for (std::uint8_t it_Button = 0; it_Button < std::to_underlying(GamepadButton::Count); ++it_Button)
            {
                if (Input::WasGamepadButtonPressed(gamepad, static_cast<GamepadButton>(it_Button)))
                {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] bool AnyKeyOrMouseButtonPressed()
        {
            for (std::uint16_t it_Key = 0; it_Key < std::to_underlying(KeyCode::Count); ++it_Key)
            {
                if (Input::WasKeyPressed(static_cast<KeyCode>(it_Key)))
                {
                    return true;
                }
            }

            for (std::uint8_t it_Button = 0; it_Button < std::to_underlying(MouseCode::Count); ++it_Button)
            {
                if (Input::WasMouseButtonPressed(static_cast<MouseCode>(it_Button)))
                {
                    return true;
                }
            }

            return false;
        }

        bool SetMapEnabled(std::string_view map, std::uint32_t player, bool enable)
        {
            if (!IsActive(player))
            {
                TR_CORE_WARN("Input: map '{}' was not {}, since there is no player {}", map, enable ? "enabled" : "disabled", player);

                return false;
            }

            PlayerState& l_Player = s_State->Players[player];
            const InputActionsData* l_Data = Sync();
            if (l_Data == nullptr)
            {
                l_Player.PendingChanges.push_back({ InputName(map), enable });

                return true;
            }

            const std::uint32_t l_Map = FindMap(*l_Data, map);
            if (l_Map == c_NoAction)
            {
                TR_CORE_WARN("Input: map '{}' was not {}, since the actions asset has no map of that name", map, enable ? "enabled" : "disabled");

                return false;
            }

            SetEnabledName(l_Player, map, enable);
            l_Player.EnabledMaps[l_Map] = enable ? 1 : 0;

            return true;
        }
    }

    InputAction InputAction::Find(std::string_view path)
    {
        if (!IsRunning() || path.empty())
        {
            return {};
        }

        const auto a_Known = std::ranges::find_if(s_State->Slots, [path](const ActionSlot& slot) { return std::string_view(slot.Path) == path; });
        if (a_Known != s_State->Slots.end())
        {
            return InputAction(static_cast<std::uint32_t>(a_Known - s_State->Slots.begin()), s_State->Session);
        }

        Sync();
        ActionSlot& l_Slot = s_State->Slots.emplace_back();
        l_Slot.Path = InputName(path);
        l_Slot.Action = Resolve(path);

        return InputAction(static_cast<std::uint32_t>(s_State->Slots.size() - 1), s_State->Session);
    }

    bool InputAction::IsValid() const
    {
        return IsRunning() && m_Session == s_State->Session && m_Slot < s_State->Slots.size() && s_State->Slots[m_Slot].Action != c_NoAction;
    }

    bool InputAction::IsPressed(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);

        return l_State != nullptr && l_State->Held;
    }

    bool InputAction::WasPressed(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);

        return l_State != nullptr && (s_State->InFixedStep ? l_State->FixedPressed : l_State->Pressed);
    }

    bool InputAction::WasReleased(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);

        return l_State != nullptr && (s_State->InFixedStep ? l_State->FixedReleased : l_State->Released);
    }

    float InputAction::ReadValue(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);
        if (l_State == nullptr)
        {
            return 0.0f;
        }

        return s_State->Locations[s_State->Slots[m_Slot].Action].Type == InputActionType::Vector2 ? glm::length(l_State->Value) : l_State->Value.x;
    }

    glm::vec2 InputAction::ReadVector2(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);

        return l_State != nullptr ? l_State->Value : glm::vec2(0.0f);
    }

    UUID InputActions::GetAsset()
    {
        return IsRunning() ? s_State->Asset : UUID();
    }

    bool InputActions::IsReady()
    {
        return Sync() != nullptr;
    }

    bool InputActions::EnableMap(std::string_view map, std::uint32_t player)
    {
        return SetMapEnabled(map, player, true);
    }

    // Its actions read as released from the next frame, so a held one is released once
    bool InputActions::DisableMap(std::string_view map, std::uint32_t player)
    {
        return SetMapEnabled(map, player, false);
    }

    // Before the asset is ready, as the change asked for last says it will be
    bool InputActions::IsMapEnabled(std::string_view map, std::uint32_t player)
    {
        if (!IsActive(player))
        {
            return false;
        }

        const PlayerState& l_Player = s_State->Players[player];
        if (Sync() == nullptr)
        {
            const auto a_Last = std::ranges::find_if(l_Player.PendingChanges | std::views::reverse, [map](const MapChange& change) { return std::string_view(change.Map) == map; });

            return a_Last != (l_Player.PendingChanges | std::views::reverse).end() && a_Last->Enable;
        }

        return HasName(l_Player.EnabledNames, map);
    }

    std::uint32_t InputActions::AddPlayer()
    {
        if (!IsRunning())
        {
            return c_NoPlayer;
        }

        for (std::uint32_t it_Player = 1; it_Player < c_MaxPlayers; ++it_Player)
        {
            PlayerState& l_Player = s_State->Players[it_Player];
            if (!l_Player.Active)
            {
                l_Player = PlayerState{};
                l_Player.Active = true;
                l_Player.FirstMapPending = true;
                Fit(l_Player);
                UpdateReadMasks();

                return it_Player;
            }
        }

        return c_NoPlayer;
    }

    void InputActions::RemovePlayer(std::uint32_t player)
    {
        if (player == 0 || !IsActive(player))
        {
            return;
        }

        s_State->Players[player] = PlayerState{};
        for (std::uint32_t& it_Assigned : s_State->DevicePlayers)
        {
            it_Assigned = it_Assigned == player ? c_NoPlayer : it_Assigned;
        }

        UpdateReadMasks();
    }

    bool InputActions::IsPlayer(std::uint32_t player)
    {
        return IsActive(player);
    }

    std::uint32_t InputActions::GetPlayerCount()
    {
        return IsRunning() ? static_cast<std::uint32_t>(std::ranges::count_if(s_State->Players, &PlayerState::Active)) : 0;
    }

    bool InputActions::AssignDevice(InputDevice device, std::uint32_t player)
    {
        if (!IsActive(player) || std::to_underlying(device) >= c_DeviceCount)
        {
            return false;
        }

        s_State->DevicePlayers[std::to_underlying(device)] = player;
        UpdateReadMasks();

        return true;
    }

    void InputActions::UnassignDevice(InputDevice device)
    {
        if (IsRunning() && std::to_underlying(device) < c_DeviceCount)
        {
            s_State->DevicePlayers[std::to_underlying(device)] = c_NoPlayer;
            UpdateReadMasks();
        }
    }

    std::uint32_t InputActions::GetDevicePlayer(InputDevice device)
    {
        if (!IsRunning() || std::to_underlying(device) >= c_DeviceCount)
        {
            return c_NoPlayer;
        }

        const std::uint32_t l_Player = s_State->DevicePlayers[std::to_underlying(device)];

        return IsActive(l_Player) ? l_Player : c_NoPlayer;
    }

    bool InputActions::ReadsDevice(std::uint32_t player, InputDevice device)
    {
        return IsActive(player) && std::to_underlying(device) < c_DeviceCount && (s_State->ReadMasks[player] & (1u << std::to_underlying(device))) != 0;
    }

    InputDevice InputActions::GetJoiningDevice()
    {
        if (!IsRunning())
        {
            return InputDevice::None;
        }

        if (GetDevicePlayer(InputDevice::KeyboardMouse) == c_NoPlayer && AnyKeyOrMouseButtonPressed())
        {
            return InputDevice::KeyboardMouse;
        }

        for (std::uint32_t it_Gamepad = 0; it_Gamepad < Input::c_MaxGamepads; ++it_Gamepad)
        {
            const InputDevice l_Device = GetGamepadDevice(it_Gamepad);
            if (GetDevicePlayer(l_Device) == c_NoPlayer && Input::IsGamepadConnected(it_Gamepad) && AnyGamepadButtonPressed(it_Gamepad))
            {
                return l_Device;
            }
        }

        return InputDevice::None;
    }

    namespace InputActionsRuntime
    {
        void Initialize()
        {
            TR_CORE_ASSERT(s_State == nullptr, "InputActions is initialized once.");

            s_State = Memory::New<State>(MemoryTag::Input);
            ResetPlayers();
            AssetManager::RegisterLoader(s_State->Loader);
        }

        void Shutdown()
        {
            if (s_State == nullptr)
            {
                return;
            }

            Stop();
            AssetManager::UnregisterLoader(s_State->Loader.GetAssetType());
            Memory::Delete(s_State);
            s_State = nullptr;
        }

        void Start(UUID asset)
        {
            Stop();

            ++s_State->Session;
            s_State->Running = true;
            s_State->Asset = asset;
            s_State->WarnedType = false;
            ResetPlayers();
            s_State->Players[0].Active = true;
            s_State->Players[0].FirstMapPending = true;
            UpdateReadMasks();
            if (!asset)
            {
                TR_CORE_INFO("Input: no input actions asset is set, so every action reads as released");

                return;
            }

            AssetManager::Acquire(asset);
        }

        // Rumble is the SceneManager's to stop. What was found and enabled goes, and the memory with it
        void Stop()
        {
            if (!s_State->Running)
            {
                return;
            }

            AssetManager::Release(s_State->Asset);
            ++s_State->Session;
            s_State->Running = false;
            s_State->InFixedStep = false;
            s_State->Asset = {};
            s_State->Data = nullptr;
            s_State->BuiltSerial = 0;
            s_State->Locations = InputVector<ActionLocation>();
            s_State->Slots = InputVector<ActionSlot>();
            ResetPlayers();
        }

        void Update()
        {
            const InputActionsData* l_Data = Sync();
            if (l_Data == nullptr)
            {
                return;
            }

            for (std::uint32_t it_Player = 0; it_Player < InputActions::c_MaxPlayers; ++it_Player)
            {
                PlayerState& l_Player = s_State->Players[it_Player];
                if (!l_Player.Active)
                {
                    continue;
                }

                const std::uint32_t l_Mask = s_State->ReadMasks[it_Player];
                for (std::size_t it_Action = 0; it_Action < s_State->Locations.size(); ++it_Action)
                {
                    const ActionLocation& l_Location = s_State->Locations[it_Action];
                    Evaluate(l_Data->Maps[l_Location.Map].Actions[l_Location.Action], l_Player.EnabledMaps[l_Location.Map] != 0, l_Mask, l_Player.Actions[it_Action]);
                }
            }
        }

        void BeginFixedStep()
        {
            if (IsRunning())
            {
                s_State->InFixedStep = true;
            }
        }

        void EndFixedStep()
        {
            if (!IsRunning())
            {
                return;
            }

            s_State->InFixedStep = false;
            for (PlayerState& it_Player : s_State->Players)
            {
                for (ActionState& it_Action : it_Player.Actions)
                {
                    it_Action.FixedPressed = false;
                    it_Action.FixedReleased = false;
                }
            }
        }
    }
}