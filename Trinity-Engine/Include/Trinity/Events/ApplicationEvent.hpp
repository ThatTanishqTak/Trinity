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
}