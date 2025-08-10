#pragma once

#include <functional>
#include <ostream>
#include <string>
#include <sstream>

namespace Trinity
{
    enum class EventType
    {
        None = 0,
        WindowResize, WindowMinimize, WindowMaximize, WindowClose,
        KeyPressed, KeyReleased,
        MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
    };

    enum EventCategory
    {
        EventCategoryNone = 0,
        EventCategoryApplication = 1 << 0,
        EventCategoryInput = 1 << 1,
        EventCategoryKeyboard = 1 << 2,
        EventCategoryMouse = 1 << 3,
        EventCategoryMouseButton = 1 << 4
    };

    class Event
    {
    public:
        bool Handled = false;

        virtual EventType GetEventType() const = 0;
        virtual const char* GetName() const = 0;
        virtual int GetCategoryFlags() const = 0;
        virtual std::string ToString() const { return GetName(); }

        bool IsInCategory(EventCategory category)
        {
            return GetCategoryFlags() & category;
        }
    };

    class EventDispatcher
    {
    public:
        EventDispatcher(Event& event) : m_Event(event) {}

        template<typename T, typename F>
        bool Dispatch(const F& func)
        {
            if (m_Event.GetEventType() == T::GetStaticType())
            {
                m_Event.Handled = func(static_cast<T&>(m_Event));

                return true;
            }

            return false;
        }

    private:
        Event& m_Event;
    };

    template<typename... Args>
    inline std::string FormatEvent(const char* prefix, Args&&... args)
    {
        std::ostringstream ss;
        ss << prefix;
        (ss << ... << std::forward<Args>(args));
        return ss.str();
    }
}

#define TR_EVENT_CLASS_TYPE(type) static EventType GetStaticType() { return EventType::type; } \
    virtual EventType GetEventType() const override { return GetStaticType(); } \
    virtual const char* GetName() const override { return #type; }

#define TR_EVENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override { return category; }

#define TR_BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)

inline std::ostream& operator<<(std::ostream& os, const Trinity::Event& e)
{
    return os << e.ToString();
}