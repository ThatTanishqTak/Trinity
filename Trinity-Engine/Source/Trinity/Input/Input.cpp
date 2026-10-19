#include "Trinity/Input/Input.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Platform.hpp"
#include "Trinity/Events/ApplicationEvent.hpp"
#include "Trinity/Events/GamepadEvent.hpp"
#include "Trinity/Events/KeyEvent.hpp"
#include "Trinity/Events/MouseEvent.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

namespace Trinity
{
    namespace
    {
        constexpr std::size_t c_KeyCount = std::to_underlying(KeyCode::Count);
        constexpr std::size_t c_MouseButtonCount = std::to_underlying(MouseCode::Count);
        constexpr std::size_t c_AxisCount = std::to_underlying(GamepadAxis::Count);
        // How near its rest an axis held as the scene gets the pads back has to come before the scene reads it, as far as Listen and an action's press point take a push to be
        constexpr float c_AxisHeldBack = 0.5f;

        // A pad's buttons held now and those that went down or up during this frame, each as bit 1 << GamepadButton, and its axes
        struct PadState
        {
            std::uint32_t Buttons = 0;
            std::uint32_t Pressed = 0;
            std::uint32_t Released = 0;
            std::array<float, c_AxisCount> Axes{};
        };

        // Each key, mouse button and pad held now, and those that went down or up during this frame. Where the mouse is, in the window's pixels, and where it was as the frame began, once it has moved at all, and how far the wheel has turned since
        struct DeviceState
        {
            std::array<bool, c_KeyCount> Keys{};
            std::array<bool, c_KeyCount> KeysPressed{};
            std::array<bool, c_KeyCount> KeysReleased{};
            std::array<bool, c_MouseButtonCount> MouseButtons{};
            std::array<bool, c_MouseButtonCount> MouseButtonsPressed{};
            std::array<bool, c_MouseButtonCount> MouseButtonsReleased{};
            MousePosition Mouse;
            MousePosition MouseAtFrameStart;
            glm::vec2 Scroll{ 0.0f };
            std::array<PadState, Input::c_MaxGamepads> Gamepads{};
        };

        // Whether a pad is in a slot, the rumble asked of it and its name
        struct GamepadState
        {
            bool Connected = false;
            float RumbleLow = 0.0f;
            float RumbleHigh = 0.0f;
            std::array<char, 128> Name{};
            std::size_t NameLength = 0;
        };

        // The devices as the window's events and the platform's readings left them, and as the scene has been let have them. The filter the scene reads by and the one asked for its next frame, and each pad's axes the scene is kept from, bit 1 << GamepadAxis, until they come back under c_AxisHeldBack. Whether the mouse has moved at all, whether reads are the scene's, and whether a window of the application had the focus at the last poll
        struct InputState
        {
            DeviceState Window;
            DeviceState Scene;
            std::array<GamepadState, Input::c_MaxGamepads> Gamepads{};
            SceneInputFilter Filter;
            SceneInputFilter NextFilter;
            std::array<std::uint32_t, Input::c_MaxGamepads> HeldBackAxes{};
            bool MouseKnown = false;
            bool ReadingScene = false;
            bool Foreground = false;
        };

        InputState s_State;

        [[nodiscard]] const DeviceState& GetDevices()
        {
            return s_State.ReadingScene ? s_State.Scene : s_State.Window;
        }

        template<std::size_t Count, typename Code>
        bool Read(const std::array<bool, Count>& states, Code code)
        {
            const std::size_t l_Index = std::to_underlying(code);

            return l_Index < states.size() && states[l_Index];
        }

        bool ReadButton(std::uint32_t buttons, GamepadButton button)
        {
            const std::size_t l_Index = std::to_underlying(button);

            return l_Index < std::to_underlying(GamepadButton::Count) && (buttons & (1u << l_Index)) != 0;
        }

        // NaN stops the motor too
        float ToMotorSpeed(float speed)
        {
            return speed > 0.0f ? std::min(speed, 1.0f) : 0.0f;
        }

        // Every key or button held goes up this frame
        template<std::size_t Count>
        void ReleaseAll(std::array<bool, Count>& held, std::array<bool, Count>& released)
        {
            for (std::size_t it_Index = 0; it_Index < Count; ++it_Index)
            {
                released[it_Index] = released[it_Index] || held[it_Index];
            }

            held.fill(false);
        }

        // A pad just connected holds nothing, though what went up as it last disconnected still did this frame
        void ClearGamepad(PadState& gamepad)
        {
            gamepad.Buttons = 0;
            gamepad.Axes.fill(0.0f);
        }

        // Every button held goes up this frame
        void ReleaseGamepad(PadState& gamepad)
        {
            gamepad.Released |= gamepad.Buttons;
            gamepad.Buttons = 0;
            gamepad.Axes.fill(0.0f);
        }

        // The buttons that went down, and the ones held that are up now
        void TakeButtons(PadState& gamepad, std::uint32_t down, std::uint32_t buttons)
        {
            gamepad.Pressed |= down;
            gamepad.Buttons |= down;
            gamepad.Released |= gamepad.Buttons & ~buttons;
            gamepad.Buttons &= buttons;
        }

        // The next frame's presses, releases, movement and scrolling count from here
        void BeginFrame(DeviceState& devices)
        {
            devices.KeysPressed.fill(false);
            devices.KeysReleased.fill(false);
            devices.MouseButtonsPressed.fill(false);
            devices.MouseButtonsReleased.fill(false);
            devices.MouseAtFrameStart = devices.Mouse;
            devices.Scroll = glm::vec2(0.0f);

            for (PadState& it_Gamepad : devices.Gamepads)
            {
                it_Gamepad.Pressed = 0;
                it_Gamepad.Released = 0;
            }
        }

        // As the scene gets the pads back, an axis held is kept from it until it comes back under c_AxisHeldBack, and the rest read as they are
        void HoldBackAxes(std::uint32_t gamepad)
        {
            const PadState& l_Window = s_State.Window.Gamepads[gamepad];
            PadState& l_Scene = s_State.Scene.Gamepads[gamepad];
            std::uint32_t& l_HeldBack = s_State.HeldBackAxes[gamepad];
            l_HeldBack = 0;
            for (std::size_t it_Axis = 0; it_Axis < c_AxisCount; ++it_Axis)
            {
                if (std::abs(l_Window.Axes[it_Axis]) >= c_AxisHeldBack)
                {
                    l_HeldBack |= 1u << it_Axis;
                }
                else
                {
                    l_Scene.Axes[it_Axis] = l_Window.Axes[it_Axis];
                }
            }
        }

        // The filter asked for, as the frame ends. What the scene loses it lets go of, with releases it reads in its next frame, and the mouse it gets back starts where the pointer is, so it does not jump there
        void ApplySceneFilter()
        {
            const SceneInputFilter& l_Next = s_State.NextFilter;
            const SceneInputFilter& l_Filter = s_State.Filter;
            DeviceState& l_Scene = s_State.Scene;
            if (l_Filter.Keyboard && !l_Next.Keyboard)
            {
                ReleaseAll(l_Scene.Keys, l_Scene.KeysReleased);
            }

            if (l_Filter.Mouse && !l_Next.Mouse)
            {
                ReleaseAll(l_Scene.MouseButtons, l_Scene.MouseButtonsReleased);
            }
            else if (!l_Filter.Mouse && l_Next.Mouse)
            {
                l_Scene.Mouse = s_State.Window.Mouse;
                l_Scene.MouseAtFrameStart = s_State.Window.Mouse;
            }

            for (std::uint32_t it_Gamepad = 0; it_Gamepad < Input::c_MaxGamepads; ++it_Gamepad)
            {
                if (l_Filter.Gamepads && !l_Next.Gamepads)
                {
                    ReleaseGamepad(l_Scene.Gamepads[it_Gamepad]);
                }
                else if (!l_Filter.Gamepads && l_Next.Gamepads)
                {
                    HoldBackAxes(it_Gamepad);
                }
            }

            s_State.Filter = l_Next;
        }

        // Takes what the platform reports into the slots, and passes connections on to the layers as events. Readings are left out while no window of the application has the focus. The scene sees a button go down only while it has the pads, and sees every one it holds go up
        class GamepadSlots final : public Platform::GamepadListener
        {
        public:
            const std::function<void(Event&)>* OnEvent = nullptr;

            void OnGamepadConnected(std::uint32_t slot, std::string_view name) override
            {
                GamepadState& l_Gamepad = s_State.Gamepads[slot];
                l_Gamepad.Connected = true;
                l_Gamepad.RumbleLow = 0.0f;
                l_Gamepad.RumbleHigh = 0.0f;
                ClearGamepad(s_State.Window.Gamepads[slot]);
                ClearGamepad(s_State.Scene.Gamepads[slot]);
                s_State.HeldBackAxes[slot] = 0;

                // Cut at a character's start, so the name stays UTF-8
                std::size_t l_Length = std::min(name.size(), l_Gamepad.Name.size());
                if (l_Length < name.size())
                {
                    while (l_Length > 0 && (static_cast<unsigned char>(name[l_Length]) & 0xC0) == 0x80)
                    {
                        --l_Length;
                    }
                }

                std::ranges::copy(name.substr(0, l_Length), l_Gamepad.Name.begin());
                l_Gamepad.NameLength = l_Length;

                const std::string_view l_Name = Input::GetGamepadName(slot);
                TR_CORE_INFO("Gamepad {} connected: {}", slot, l_Name);

                GamepadConnectedEvent l_Event(slot, l_Name);
                (*OnEvent)(l_Event);
            }

            void OnGamepadDisconnected(std::uint32_t slot) override
            {
                GamepadState& l_Gamepad = s_State.Gamepads[slot];
                ReleaseGamepad(s_State.Window.Gamepads[slot]);
                ReleaseGamepad(s_State.Scene.Gamepads[slot]);
                l_Gamepad.Connected = false;
                l_Gamepad.RumbleLow = 0.0f;
                l_Gamepad.RumbleHigh = 0.0f;

                const std::string_view l_Name(l_Gamepad.Name.data(), l_Gamepad.NameLength);
                TR_CORE_INFO("Gamepad {} disconnected: {}", slot, l_Name);

                GamepadDisconnectedEvent l_Event(slot, l_Name);
                (*OnEvent)(l_Event);
                l_Gamepad.NameLength = 0;
            }

            void OnGamepadReading(std::uint32_t slot, const Platform::GamepadReading& reading) override
            {
                if (!s_State.Foreground)
                {
                    return;
                }

                PadState& l_Window = s_State.Window.Gamepads[slot];
                const std::uint32_t l_Down = reading.Buttons & ~l_Window.Buttons;
                TakeButtons(l_Window, l_Down, reading.Buttons);
                l_Window.Axes = reading.Axes;

                const bool l_SceneHasPads = s_State.Filter.Gamepads;
                PadState& l_Scene = s_State.Scene.Gamepads[slot];
                TakeButtons(l_Scene, l_SceneHasPads ? l_Down : 0, reading.Buttons);
                if (!l_SceneHasPads)
                {
                    return;
                }

                std::uint32_t& l_HeldBack = s_State.HeldBackAxes[slot];
                for (std::size_t it_Axis = 0; it_Axis < c_AxisCount; ++it_Axis)
                {
                    const std::uint32_t l_Bit = 1u << it_Axis;
                    if ((l_HeldBack & l_Bit) != 0 && std::abs(reading.Axes[it_Axis]) < c_AxisHeldBack)
                    {
                        l_HeldBack &= ~l_Bit;
                    }

                    l_Scene.Axes[it_Axis] = (l_HeldBack & l_Bit) != 0 ? 0.0f : reading.Axes[it_Axis];
                }
            }
        };

        GamepadSlots s_GamepadSlots;
    }

    bool Input::IsKeyPressed(KeyCode key)
    {
        return Read(GetDevices().Keys, key);
    }

    bool Input::WasKeyPressed(KeyCode key)
    {
        return Read(GetDevices().KeysPressed, key);
    }

    bool Input::WasKeyReleased(KeyCode key)
    {
        return Read(GetDevices().KeysReleased, key);
    }

    bool Input::IsMouseButtonPressed(MouseCode button)
    {
        return Read(GetDevices().MouseButtons, button);
    }

    bool Input::WasMouseButtonPressed(MouseCode button)
    {
        return Read(GetDevices().MouseButtonsPressed, button);
    }

    bool Input::WasMouseButtonReleased(MouseCode button)
    {
        return Read(GetDevices().MouseButtonsReleased, button);
    }

    // The scene's in its image's pixels
    MousePosition Input::GetMousePosition()
    {
        if (!s_State.ReadingScene)
        {
            return s_State.Window.Mouse;
        }

        const SceneInputFilter& l_Filter = s_State.Filter;
        const MousePosition& l_Mouse = s_State.Scene.Mouse;

        return { (l_Mouse.X - l_Filter.MouseOrigin.x) / l_Filter.MouseScale, (l_Mouse.Y - l_Filter.MouseOrigin.y) / l_Filter.MouseScale };
    }

    glm::vec2 Input::GetMouseDelta()
    {
        const DeviceState& l_Devices = GetDevices();
        const glm::vec2 l_Delta(l_Devices.Mouse.X - l_Devices.MouseAtFrameStart.X, l_Devices.Mouse.Y - l_Devices.MouseAtFrameStart.Y);

        return s_State.ReadingScene ? l_Delta / s_State.Filter.MouseScale : l_Delta;
    }

    glm::vec2 Input::GetMouseScroll()
    {
        return GetDevices().Scroll;
    }

    bool Input::IsGamepadConnected(std::uint32_t gamepad)
    {
        return gamepad < c_MaxGamepads && s_State.Gamepads[gamepad].Connected;
    }

    std::string_view Input::GetGamepadName(std::uint32_t gamepad)
    {
        if (!IsGamepadConnected(gamepad))
        {
            return {};
        }

        const GamepadState& l_Gamepad = s_State.Gamepads[gamepad];

        return { l_Gamepad.Name.data(), l_Gamepad.NameLength };
    }

    bool Input::IsGamepadButtonPressed(std::uint32_t gamepad, GamepadButton button)
    {
        return gamepad < c_MaxGamepads && ReadButton(GetDevices().Gamepads[gamepad].Buttons, button);
    }

    bool Input::WasGamepadButtonPressed(std::uint32_t gamepad, GamepadButton button)
    {
        return gamepad < c_MaxGamepads && ReadButton(GetDevices().Gamepads[gamepad].Pressed, button);
    }

    bool Input::WasGamepadButtonReleased(std::uint32_t gamepad, GamepadButton button)
    {
        return gamepad < c_MaxGamepads && ReadButton(GetDevices().Gamepads[gamepad].Released, button);
    }

    float Input::GetGamepadAxis(std::uint32_t gamepad, GamepadAxis axis)
    {
        const std::size_t l_Index = std::to_underlying(axis);

        return gamepad < c_MaxGamepads && l_Index < c_AxisCount ? GetDevices().Gamepads[gamepad].Axes[l_Index] : 0.0f;
    }

    void Input::SetGamepadRumble(std::uint32_t gamepad, float lowFrequency, float highFrequency)
    {
        if (!IsGamepadConnected(gamepad))
        {
            return;
        }

        GamepadState& l_Gamepad = s_State.Gamepads[gamepad];
        l_Gamepad.RumbleLow = ToMotorSpeed(lowFrequency);
        l_Gamepad.RumbleHigh = ToMotorSpeed(highFrequency);
        if (s_State.Foreground)
        {
            Platform::SetGamepadRumble(gamepad, l_Gamepad.RumbleLow, l_Gamepad.RumbleHigh);
        }
    }

    void Input::StopGamepadRumble()
    {
        for (std::uint32_t it_Gamepad = 0; it_Gamepad < c_MaxGamepads; ++it_Gamepad)
        {
            SetGamepadRumble(it_Gamepad, 0.0f, 0.0f);
        }
    }

    // A scale that is not a positive number reads the mouse as the window has it
    void Input::SetSceneFilter(const SceneInputFilter& filter)
    {
        s_State.NextFilter = filter;
        if (!(filter.MouseScale > 0.0f))
        {
            s_State.NextFilter.MouseScale = 1.0f;
        }
    }

    // Losing the focus releases every pad and stops its rumble, and getting it back starts the rumble again and reads each pad as it is now, skipping what happened in between
    void Input::PollGamepads(const std::function<void(Event&)>& onEvent)
    {
        const bool l_Foreground = Platform::IsForeground();
        const bool l_Resync = l_Foreground && !s_State.Foreground;
        if (l_Foreground != s_State.Foreground)
        {
            s_State.Foreground = l_Foreground;
            for (std::uint32_t it_Gamepad = 0; it_Gamepad < c_MaxGamepads; ++it_Gamepad)
            {
                const GamepadState& l_Gamepad = s_State.Gamepads[it_Gamepad];
                if (!l_Gamepad.Connected)
                {
                    continue;
                }

                if (!l_Foreground)
                {
                    ReleaseGamepad(s_State.Window.Gamepads[it_Gamepad]);
                    ReleaseGamepad(s_State.Scene.Gamepads[it_Gamepad]);
                }

                Platform::SetGamepadRumble(it_Gamepad, l_Foreground ? l_Gamepad.RumbleLow : 0.0f, l_Foreground ? l_Gamepad.RumbleHigh : 0.0f);
            }
        }

        s_GamepadSlots.OnEvent = &onEvent;
        Platform::PollGamepads(s_GamepadSlots, l_Resync);
        s_GamepadSlots.OnEvent = nullptr;
    }

    // The filter asked for this frame is taken after the edges are cleared, so what the scene lets go of by it is released in its next frame
    void Input::EndFrame()
    {
        BeginFrame(s_State.Window);
        BeginFrame(s_State.Scene);
        ApplySceneFilter();
    }

    void Input::InitializeGamepads()
    {
        Platform::InitializeGamepads();
    }

    // Stops every pad's rumble, with no events, since the layers are gone
    void Input::ShutdownGamepads()
    {
        Platform::ShutdownGamepads();
        s_State.Gamepads = {};
        s_State.Window.Gamepads = {};
        s_State.Scene.Gamepads = {};
        s_State.HeldBackAxes = {};
        s_State.Foreground = false;
    }

    bool Input::SetReadingScene(bool reading)
    {
        return std::exchange(s_State.ReadingScene, reading);
    }

    // The scene sees a key or button go down only while the filter lets it in, and sees every one it holds go up. A repeat brings in no key the scene did not see go down
    void Input::OnEvent(const Event& event)
    {
        DeviceState& l_Window = s_State.Window;
        DeviceState& l_Scene = s_State.Scene;
        const SceneInputFilter& l_Filter = s_State.Filter;
        switch (event.GetEventType())
        {
            case EventType::KeyPressed:
            {
                const auto& l_Pressed = static_cast<const KeyPressedEvent&>(event);
                const std::size_t l_Index = std::to_underlying(l_Pressed.GetKeyCode());
                if (l_Index < c_KeyCount)
                {
                    l_Window.KeysPressed[l_Index] = l_Window.KeysPressed[l_Index] || !l_Pressed.IsRepeat();
                    l_Window.Keys[l_Index] = true;
                    if (l_Filter.Keyboard && !l_Pressed.IsRepeat())
                    {
                        l_Scene.KeysPressed[l_Index] = true;
                        l_Scene.Keys[l_Index] = true;
                    }
                }

                break;
            }
            case EventType::KeyReleased:
            {
                const std::size_t l_Index = std::to_underlying(static_cast<const KeyEvent&>(event).GetKeyCode());
                if (l_Index < c_KeyCount)
                {
                    l_Window.KeysReleased[l_Index] = true;
                    l_Window.Keys[l_Index] = false;
                    l_Scene.KeysReleased[l_Index] = l_Scene.KeysReleased[l_Index] || l_Scene.Keys[l_Index];
                    l_Scene.Keys[l_Index] = false;
                }

                break;
            }
            case EventType::MouseButtonPressed:
            case EventType::MouseButtonReleased:
            {
                const std::size_t l_Index = std::to_underlying(static_cast<const MouseButtonEvent&>(event).GetMouseButton());
                if (l_Index < c_MouseButtonCount)
                {
                    const bool l_Down = event.GetEventType() == EventType::MouseButtonPressed;
                    (l_Down ? l_Window.MouseButtonsPressed : l_Window.MouseButtonsReleased)[l_Index] = true;
                    l_Window.MouseButtons[l_Index] = l_Down;
                    if (l_Down && l_Filter.Mouse && l_Filter.MouseOver)
                    {
                        l_Scene.MouseButtonsPressed[l_Index] = true;
                        l_Scene.MouseButtons[l_Index] = true;
                    }
                    else if (!l_Down)
                    {
                        l_Scene.MouseButtonsReleased[l_Index] = l_Scene.MouseButtonsReleased[l_Index] || l_Scene.MouseButtons[l_Index];
                        l_Scene.MouseButtons[l_Index] = false;
                    }
                }

                break;
            }
            case EventType::MouseMoved:
            {
                const auto& l_Moved = static_cast<const MouseMovedEvent&>(event);
                const MousePosition l_Position{ l_Moved.GetX(), l_Moved.GetY() };
                l_Window.Mouse = l_Position;
                if (l_Filter.Mouse)
                {
                    l_Scene.Mouse = l_Position;
                }

                if (!std::exchange(s_State.MouseKnown, true))
                {
                    l_Window.MouseAtFrameStart = l_Position;
                    l_Scene.Mouse = l_Position;
                    l_Scene.MouseAtFrameStart = l_Position;
                }

                break;
            }
            case EventType::MouseScrolled:
            {
                const auto& l_Scrolled = static_cast<const MouseScrolledEvent&>(event);
                const glm::vec2 l_Offset(l_Scrolled.GetXOffset(), l_Scrolled.GetYOffset());
                l_Window.Scroll += l_Offset;
                if (l_Filter.Mouse && l_Filter.MouseOver)
                {
                    l_Scene.Scroll += l_Offset;
                }

                break;
            }
            case EventType::WindowLostFocus:
            {
                l_Window.Keys.fill(false);
                l_Window.MouseButtons.fill(false);
                ReleaseAll(l_Scene.Keys, l_Scene.KeysReleased);
                ReleaseAll(l_Scene.MouseButtons, l_Scene.MouseButtonsReleased);

                break;
            }
            default:
            {
                break;
            }
        }
    }
}