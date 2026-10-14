#include "Trinity/Input/Input.hpp"

#include "Trinity/Events/ApplicationEvent.hpp"
#include "Trinity/Events/KeyEvent.hpp"
#include "Trinity/Events/MouseEvent.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace Trinity
{
    namespace
    {
        // Each key and button held now, and those that went down or up during this frame. Where the mouse was as the frame began, once it has moved at all, and how far the wheel has turned since
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
        };

        InputState s_State;

        template<std::size_t Count, typename Code>
        bool Read(const std::array<bool, Count>& states, Code code)
        {
            const std::size_t l_Index = std::to_underlying(code);

            return l_Index < states.size() && states[l_Index];
        }
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

    void Input::EndFrame()
    {
        s_State.KeysPressed.fill(false);
        s_State.KeysReleased.fill(false);
        s_State.MouseButtonsPressed.fill(false);
        s_State.MouseButtonsReleased.fill(false);
        s_State.MouseAtFrameStart = s_State.Mouse;
        s_State.Scroll = glm::vec2(0.0f);
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