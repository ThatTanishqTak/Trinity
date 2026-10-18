#include "Trinity/Input/Input.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Platform.hpp"
#include "Trinity/Events/ApplicationEvent.hpp"
#include "Trinity/Events/GamepadEvent.hpp"
#include "Trinity/Events/KeyEvent.hpp"
#include "Trinity/Events/MouseEvent.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

namespace Trinity
{
    namespace
    {
        // A pad's buttons held now and those that went down or up during this frame, each as bit 1 << GamepadButton, its axes, the rumble asked of it and its name
        struct GamepadState
        {
            bool Connected = false;
            std::uint32_t Buttons = 0;
            std::uint32_t Pressed = 0;
            std::uint32_t Released = 0;
            std::array<float, std::to_underlying(GamepadAxis::Count)> Axes{};
            float RumbleLow = 0.0f;
            float RumbleHigh = 0.0f;
            std::array<char, 128> Name{};
            std::size_t NameLength = 0;
        };

        // Each key and button held now, and those that went down or up during this frame. Where the mouse was as the frame began, once it has moved at all, and how far the wheel has turned since. Each gamepad's slot, and whether a window of the application had the focus at the last poll
        struct InputState
        {
            std::array<bool, std::to_underlying(KeyCode::Count)> Keys{};
            std::array<bool, std::to_underlying(KeyCode::Count)> KeysPressed{};
            std::array<bool, std::to_underlying(KeyCode::Count)> KeysReleased{};
            std::array<bool, std::to_underlying(MouseCode::Count)> MouseButtons{};
            std::array<bool, std::to_underlying(MouseCode::Count)> MouseButtonsPressed{};
            std::array<bool, std::to_underlying(MouseCode::Count)> MouseButtonsReleased{};
            MousePosition Mouse;
            MousePosition MouseAtFrameStart;
            bool MouseKnown = false;
            glm::vec2 Scroll{ 0.0f };
            std::array<GamepadState, Input::c_MaxGamepads> Gamepads{};
            bool Foreground = false;
        };

        InputState s_State;

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

        // Every button held goes up this frame
        void ReleaseGamepad(GamepadState& gamepad)
        {
            gamepad.Released |= gamepad.Buttons;
            gamepad.Buttons = 0;
            gamepad.Axes.fill(0.0f);
        }

        // Takes what the platform reports into the slots, and passes connections on to the layers as events. Readings are left out while no window of the application has the focus
        class GamepadSlots final : public Platform::GamepadListener
        {
        public:
            const std::function<void(Event&)>* OnEvent = nullptr;

            void OnGamepadConnected(std::uint32_t slot, std::string_view name) override
            {
                GamepadState& l_Gamepad = s_State.Gamepads[slot];
                l_Gamepad.Connected = true;
                l_Gamepad.Buttons = 0;
                l_Gamepad.Axes.fill(0.0f);
                l_Gamepad.RumbleLow = 0.0f;
                l_Gamepad.RumbleHigh = 0.0f;

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
                ReleaseGamepad(l_Gamepad);
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

                GamepadState& l_Gamepad = s_State.Gamepads[slot];
                l_Gamepad.Pressed |= reading.Buttons & ~l_Gamepad.Buttons;
                l_Gamepad.Released |= l_Gamepad.Buttons & ~reading.Buttons;
                l_Gamepad.Buttons = reading.Buttons;
                l_Gamepad.Axes = reading.Axes;
            }
        };

        GamepadSlots s_GamepadSlots;
    }

    bool Input::IsKeyPressed(KeyCode key)
    {
        return Read(s_State.Keys, key);
    }

    bool Input::WasKeyPressed(KeyCode key)
    {
        return Read(s_State.KeysPressed, key);
    }

    bool Input::WasKeyReleased(KeyCode key)
    {
        return Read(s_State.KeysReleased, key);
    }

    bool Input::IsMouseButtonPressed(MouseCode button)
    {
        return Read(s_State.MouseButtons, button);
    }

    bool Input::WasMouseButtonPressed(MouseCode button)
    {
        return Read(s_State.MouseButtonsPressed, button);
    }

    bool Input::WasMouseButtonReleased(MouseCode button)
    {
        return Read(s_State.MouseButtonsReleased, button);
    }

    MousePosition Input::GetMousePosition()
    {
        return s_State.Mouse;
    }

    glm::vec2 Input::GetMouseDelta()
    {
        return { s_State.Mouse.X - s_State.MouseAtFrameStart.X, s_State.Mouse.Y - s_State.MouseAtFrameStart.Y };
    }

    glm::vec2 Input::GetMouseScroll()
    {
        return s_State.Scroll;
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
        return gamepad < c_MaxGamepads && ReadButton(s_State.Gamepads[gamepad].Buttons, button);
    }

    bool Input::WasGamepadButtonPressed(std::uint32_t gamepad, GamepadButton button)
    {
        return gamepad < c_MaxGamepads && ReadButton(s_State.Gamepads[gamepad].Pressed, button);
    }

    bool Input::WasGamepadButtonReleased(std::uint32_t gamepad, GamepadButton button)
    {
        return gamepad < c_MaxGamepads && ReadButton(s_State.Gamepads[gamepad].Released, button);
    }

    float Input::GetGamepadAxis(std::uint32_t gamepad, GamepadAxis axis)
    {
        const std::size_t l_Index = std::to_underlying(axis);

        return gamepad < c_MaxGamepads && l_Index < std::to_underlying(GamepadAxis::Count) ? s_State.Gamepads[gamepad].Axes[l_Index] : 0.0f;
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
                GamepadState& l_Gamepad = s_State.Gamepads[it_Gamepad];
                if (!l_Gamepad.Connected)
                {
                    continue;
                }

                if (!l_Foreground)
                {
                    ReleaseGamepad(l_Gamepad);
                }

                Platform::SetGamepadRumble(it_Gamepad, l_Foreground ? l_Gamepad.RumbleLow : 0.0f, l_Foreground ? l_Gamepad.RumbleHigh : 0.0f);
            }
        }

        s_GamepadSlots.OnEvent = &onEvent;
        Platform::PollGamepads(s_GamepadSlots, l_Resync);
        s_GamepadSlots.OnEvent = nullptr;
    }

    void Input::EndFrame()
    {
        s_State.KeysPressed.fill(false);
        s_State.KeysReleased.fill(false);
        s_State.MouseButtonsPressed.fill(false);
        s_State.MouseButtonsReleased.fill(false);
        s_State.MouseAtFrameStart = s_State.Mouse;
        s_State.Scroll = glm::vec2(0.0f);

        for (GamepadState& it_Gamepad : s_State.Gamepads)
        {
            it_Gamepad.Pressed = 0;
            it_Gamepad.Released = 0;
        }
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
        s_State.Foreground = false;
    }

    void Input::OnEvent(const Event& event)
    {
        switch (event.GetEventType())
        {
            case EventType::KeyPressed:
            {
                const auto& l_Pressed = static_cast<const KeyPressedEvent&>(event);
                const std::size_t l_Index = std::to_underlying(l_Pressed.GetKeyCode());
                if (l_Index < s_State.Keys.size())
                {
                    s_State.KeysPressed[l_Index] = s_State.KeysPressed[l_Index] || !l_Pressed.IsRepeat();
                    s_State.Keys[l_Index] = true;
                }

                break;
            }
            case EventType::KeyReleased:
            {
                const std::size_t l_Index = std::to_underlying(static_cast<const KeyEvent&>(event).GetKeyCode());
                if (l_Index < s_State.Keys.size())
                {
                    s_State.KeysReleased[l_Index] = true;
                    s_State.Keys[l_Index] = false;
                }

                break;
            }
            case EventType::MouseButtonPressed:
            case EventType::MouseButtonReleased:
            {
                const std::size_t l_Index = std::to_underlying(static_cast<const MouseButtonEvent&>(event).GetMouseButton());
                if (l_Index < s_State.MouseButtons.size())
                {
                    const bool l_Down = event.GetEventType() == EventType::MouseButtonPressed;
                    (l_Down ? s_State.MouseButtonsPressed : s_State.MouseButtonsReleased)[l_Index] = true;
                    s_State.MouseButtons[l_Index] = l_Down;
                }

                break;
            }
            case EventType::MouseMoved:
            {
                const auto& l_Moved = static_cast<const MouseMovedEvent&>(event);
                s_State.Mouse = { l_Moved.GetX(), l_Moved.GetY() };
                if (!std::exchange(s_State.MouseKnown, true))
                {
                    s_State.MouseAtFrameStart = s_State.Mouse;
                }

                break;
            }
            case EventType::MouseScrolled:
            {
                const auto& l_Scrolled = static_cast<const MouseScrolledEvent&>(event);
                s_State.Scroll += glm::vec2(l_Scrolled.GetXOffset(), l_Scrolled.GetYOffset());

                break;
            }
            case EventType::WindowLostFocus:
            {
                s_State.Keys.fill(false);
                s_State.MouseButtons.fill(false);

                break;
            }
            default:
            {
                break;
            }
        }
    }
}