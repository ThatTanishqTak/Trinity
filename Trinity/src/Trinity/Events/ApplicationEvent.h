#pragma once

#include "Trinity/Events/Event.h"

#include <sstream>

namespace Trinity
{
    class WindowResizeEvent : public Event
    {
    public:
        WindowResizeEvent(unsigned int width, unsigned int height) : m_Width(width), m_Height(height)
        {

        }

        unsigned int GetWidth() const { return m_Width; }
        unsigned int GetHeight() const { return m_Height; }

        std::string ToString() const override
        {
            std::stringstream ss;
            ss << "WindowResizeEvent: " << m_Width << ", " << m_Height;

            return ss.str();
        }

        TR_EVENT_CLASS_TYPE(WindowResize)
            TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)

    private:
        unsigned int m_Width, m_Height;
    };

    class WindowCloseEvent : public Event
    {
    public:
        WindowCloseEvent() = default;

        TR_EVENT_CLASS_TYPE(WindowClose)
            TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    class WindowMinimizeEvent : public Event
    {
    public:
        WindowMinimizeEvent() = default;

        TR_EVENT_CLASS_TYPE(WindowMinimize)
            TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    class WindowMaximizeEvent : public Event
    {
    public:
        WindowMaximizeEvent() = default;

        TR_EVENT_CLASS_TYPE(WindowMaximize)
            TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };
}