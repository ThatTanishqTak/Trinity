#pragma once

#include "Trinity/Events/Event.hpp"

namespace Trinity
{
    class TRINITY_API WindowCloseEvent final : public Event
    {
    public:
        TR_EVENT_CLASS_TYPE(WindowClose)
            TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    class TRINITY_API WindowResizeEvent final : public Event
    {
    public:
        WindowResizeEvent(std::uint32_t width, std::uint32_t height) : m_Width(width), m_Height(height)
        {

        }

        [[nodiscard]] std::uint32_t GetWidth() const { return m_Width; }
        [[nodiscard]] std::uint32_t GetHeight() const { return m_Height; }

        [[nodiscard]] std::string ToString() const override
        {
            return std::format("WindowResize: {}x{}", m_Width, m_Height);
        }

        TR_EVENT_CLASS_TYPE(WindowResize)
            TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)

    private:
        std::uint32_t m_Width;
        std::uint32_t m_Height;
    };

    // The top left of the client area, in screen pixels
    class TRINITY_API WindowMovedEvent final : public Event
    {
    public:
        WindowMovedEvent(std::int32_t x, std::int32_t y) : m_X(x), m_Y(y)
        {

        }

        [[nodiscard]] std::int32_t GetX() const { return m_X; }
        [[nodiscard]] std::int32_t GetY() const { return m_Y; }

        [[nodiscard]] std::string ToString() const override
        {
            return std::format("WindowMoved: {}, {}", m_X, m_Y);
        }

        TR_EVENT_CLASS_TYPE(WindowMoved)
            TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)

    private:
        std::int32_t m_X;
        std::int32_t m_Y;
    };

    class TRINITY_API WindowFocusEvent final : public Event
    {
    public:
        TR_EVENT_CLASS_TYPE(WindowFocus)
            TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    class TRINITY_API WindowLostFocusEvent final : public Event
    {
    public:
        TR_EVENT_CLASS_TYPE(WindowLostFocus)
            TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };

    // Scale 1 is 96 dots per inch
    class TRINITY_API WindowDpiChangedEvent final : public Event
    {
    public:
        explicit WindowDpiChangedEvent(float scale) : m_Scale(scale)
        {

        }

        [[nodiscard]] float GetScale() const { return m_Scale; }

        [[nodiscard]] std::string ToString() const override
        {
            return std::format("WindowDpiChanged: {}", m_Scale);
        }

        TR_EVENT_CLASS_TYPE(WindowDpiChanged)
            TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)

    private:
        float m_Scale;
    };

    // Monitors were added, removed, moved or resized, so any list of them is out of date
    class TRINITY_API MonitorsChangedEvent final : public Event
    {
    public:
        TR_EVENT_CLASS_TYPE(MonitorsChanged)
            TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };
}