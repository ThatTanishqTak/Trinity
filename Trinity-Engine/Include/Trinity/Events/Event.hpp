#pragma once

#include "Trinity/Core/Base.hpp"

#include <concepts>
#include <cstdint>
#include <format>
#include <string>

namespace Trinity
{
    enum class EventType : std::uint8_t
    {
        None = 0,
        WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowDpiChanged,
        KeyPressed, KeyReleased, KeyTyped,
        MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled, MouseLeft
    };

    enum EventCategory : std::uint32_t
    {
        EventCategoryNone = 0,
        EventCategoryApplication = TR_BIT(0),
        EventCategoryInput = TR_BIT(1),
        EventCategoryKeyboard = TR_BIT(2),
        EventCategoryMouse = TR_BIT(3),
        EventCategoryMouseButton = TR_BIT(4)
    };

#define TR_EVENT_CLASS_TYPE(type)                                                   \
    static EventType GetStaticType() { return EventType::type; }                    \
    EventType GetEventType() const override { return GetStaticType(); }             \
    const char* GetName() const override { return #type; }

#define TR_EVENT_CLASS_CATEGORY(category) \
    std::uint32_t GetCategoryFlags() const override { return category; }

    class TRINITY_API Event
    {
    public:
        virtual ~Event() = default;

        bool Handled = false;

        [[nodiscard]] virtual EventType GetEventType() const = 0;
        [[nodiscard]] virtual const char* GetName() const = 0;
        [[nodiscard]] virtual std::uint32_t GetCategoryFlags() const = 0;
        [[nodiscard]] virtual std::string ToString() const { return GetName(); }

        [[nodiscard]] bool IsInCategory(EventCategory category) const
        {
            return (GetCategoryFlags() & category) != 0;
        }
    };

    class EventDispatcher
    {
    public:
        explicit EventDispatcher(Event& event) : m_Event(event) {}

        template<std::derived_from<Event> T, std::predicate<T&> F>
        bool Dispatch(const F& handler)
        {
            if (m_Event.GetEventType() != T::GetStaticType())
            {
                return false;
            }

            m_Event.Handled |= handler(static_cast<T&>(m_Event));
            
            return true;
        }

    private:
        Event& m_Event;
    };
}

template<std::derived_from<Trinity::Event> T>
struct std::formatter<T> : std::formatter<std::string>
{
    auto format(const T& event, std::format_context& context) const
    {
        return std::formatter<std::string>::format(event.ToString(), context);
    }
};