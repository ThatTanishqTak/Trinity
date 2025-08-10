#pragma once

#include "Trinity/Events/Event.h"

namespace Trinity
{
    class MouseMovedEvent : public Event
    {
    public:
        MouseMovedEvent(float x, float y) : m_MouseX(x), m_MouseY(y)
        {

        }

        float GetX() const { return m_MouseX; }
        float GetY() const { return m_MouseY; }

        std::string ToString() const override
        {
            return FormatEvent("MouseMovedEvent: ", m_MouseX, ", ", m_MouseY);
        }

        TR_EVENT_CLASS_TYPE(MouseMoved)
            TR_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

    private:
        float m_MouseX, m_MouseY;
    };

    class MouseScrolledEvent : public Event
    {
    public:
        MouseScrolledEvent(float xOffset, float yOffset)
            : m_XOffset(xOffset), m_YOffset(yOffset) {
        }

        float GetXOffset() const { return m_XOffset; }
        float GetYOffset() const { return m_YOffset; }

        std::string ToString() const override
        {
            return FormatEvent("MouseScrolledEvent: ", m_XOffset, ", ", m_YOffset);
        }

        TR_EVENT_CLASS_TYPE(MouseScrolled)
            TR_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

    private:
        float m_XOffset, m_YOffset;
    };

    class MouseButtonEvent : public Event
    {
    public:
        int GetMouseButton() const { return m_Button; }

        TR_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput | EventCategoryMouseButton)

    protected:
        MouseButtonEvent(int button) : m_Button(button) {}

        int m_Button;
    };

    class MouseButtonPressedEvent : public MouseButtonEvent
    {
    public:
        MouseButtonPressedEvent(int button) : MouseButtonEvent(button)
        {

        }

        std::string ToString() const override
        {
            return FormatEvent("MouseButtonPressedEvent: ", m_Button);
        }

        TR_EVENT_CLASS_TYPE(MouseButtonPressed)
    };

    class MouseButtonReleasedEvent : public MouseButtonEvent
    {
    public:
        MouseButtonReleasedEvent(int button) : MouseButtonEvent(button)
        {

        }

        std::string ToString() const override
        {
            return FormatEvent("MouseButtonReleasedEvent: ", m_Button);
        }

        TR_EVENT_CLASS_TYPE(MouseButtonReleased)
    };
}