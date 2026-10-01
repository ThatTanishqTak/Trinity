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
        struct InputState
        {
            std::array<bool, std::to_underlying(KeyCode::Count)> Keys{};
            std::array<bool, std::to_underlying(MouseCode::Count)> MouseButtons{};
            MousePosition Mouse;
        };

        InputState s_State;
    }

    bool Input::IsKeyPressed(KeyCode key)
    {
        const std::size_t l_Index = std::to_underlying(key);

        return l_Index < s_State.Keys.size() && s_State.Keys[l_Index];
    }

    bool Input::IsMouseButtonPressed(MouseCode button)
    {
        const std::size_t l_Index = std::to_underlying(button);

        return l_Index < s_State.MouseButtons.size() && s_State.MouseButtons[l_Index];
    }

    MousePosition Input::GetMousePosition()
    {
        return s_State.Mouse;
    }

    void Input::OnEvent(const Event& event)
    {
        switch (event.GetEventType())
        {
            case EventType::KeyPressed:
            case EventType::KeyReleased:
            {
                const std::size_t l_Index = std::to_underlying(static_cast<const KeyEvent&>(event).GetKeyCode());
                if (l_Index < s_State.Keys.size())
                {
                    s_State.Keys[l_Index] = event.GetEventType() == EventType::KeyPressed;
                }

                break;
            }
            case EventType::MouseButtonPressed:
            case EventType::MouseButtonReleased:
            {
                const std::size_t l_Index = std::to_underlying(static_cast<const MouseButtonEvent&>(event).GetMouseButton());
                if (l_Index < s_State.MouseButtons.size())
                {
                    s_State.MouseButtons[l_Index] = event.GetEventType() == EventType::MouseButtonPressed;
                }

                break;
            }
            case EventType::MouseMoved:
            {
                const auto& l_Moved = static_cast<const MouseMovedEvent&>(event);
                s_State.Mouse = { l_Moved.GetX(), l_Moved.GetY() };
                
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