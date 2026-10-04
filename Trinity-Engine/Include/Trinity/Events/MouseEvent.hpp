#pragma once

#include "Trinity/Events/Event.hpp"
#include "Trinity/Input/MouseCodes.hpp"

namespace Trinity
{
    class TRINITY_API MouseMovedEvent final : public Event
    {
    public:
        MouseMovedEvent(float x, float y) : m_X(x), m_Y(y)
        {

        }

        [[nodiscard]] float GetX() const { return m_X; }
        [[nodiscard]] float GetY() const { return m_Y; }

        [[nodiscard]] std::string ToString() const override
        {
            return std::format("MouseMoved: {}, {}", m_X, m_Y);
        }

        TR_EVENT_CLASS_TYPE(MouseMoved)
            TR_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

    private:
        float m_X;
        float m_Y;
    };

    class TRINITY_API MouseScrolledEvent final : public Event
    {
    public:
        MouseScrolledEvent(float xOffset, float yOffset) : m_XOffset(xOffset), m_YOffset(yOffset) {}

        [[nodiscard]] float GetXOffset() const { return m_XOffset; }
        [[nodiscard]] float GetYOffset() const { return m_YOffset; }

        [[nodiscard]] std::string ToString() const override
        {
            return std::format("MouseScrolled: {}, {}", m_XOffset, m_YOffset);
        }

        TR_EVENT_CLASS_TYPE(MouseScrolled)
            TR_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

    private:
        float m_XOffset;
        float m_YOffset;
    };

    class TRINITY_API MouseLeftEvent final : public Event
    {
    public:
        TR_EVENT_CLASS_TYPE(MouseLeft)
            TR_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
    };

    class TRINITY_API MouseButtonEvent : public Event
    {
    public:
        [[nodiscard]] MouseCode GetMouseButton() const { return m_Button; }

        TR_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryMouseButton | EventCategoryInput)

    protected:
        explicit MouseButtonEvent(MouseCode button) : m_Button(button)
        {

        }

        MouseCode m_Button;
    };

    class TRINITY_API MouseButtonPressedEvent final : public MouseButtonEvent
    {
    public:
        explicit MouseButtonPressedEvent(MouseCode button) : MouseButtonEvent(button)
        {

        }

        [[nodiscard]] std::string ToString() const override
        {
            return std::format("MouseButtonPressed: {}", std::to_underlying(m_Button));
        }

        TR_EVENT_CLASS_TYPE(MouseButtonPressed)
    };

    class TRINITY_API MouseButtonReleasedEvent final : public MouseButtonEvent
    {
    public:
        explicit MouseButtonReleasedEvent(MouseCode button) : MouseButtonEvent(button)
        {

        }

        [[nodiscard]] std::string ToString() const override
        {
            return std::format("MouseButtonReleased: {}", std::to_underlying(m_Button));
        }

        TR_EVENT_CLASS_TYPE(MouseButtonReleased)
    };
}