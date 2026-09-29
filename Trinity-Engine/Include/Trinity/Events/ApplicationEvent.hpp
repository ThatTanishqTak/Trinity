#pragma once

#include "Trinity/Events/Event.hpp"

#include <format>

namespace Trinity
{
	class WindowCloseEvent : public Event
	{
	public:
		WindowCloseEvent() = default;

		TR_EVENT_CLASS_TYPE(WindowClose)
			TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowResizeEvent : public Event
	{
	public:
		WindowResizeEvent(uint32_t width, uint32_t height) : m_Width(width), m_Height(height) {}

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		std::string ToString() const override { return std::format("WindowResizeEvent: {}x{}", m_Width, m_Height); }

		TR_EVENT_CLASS_TYPE(WindowResize)
			TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
	};

	class WindowFocusEvent : public Event
	{
	public:
		WindowFocusEvent() = default;

		TR_EVENT_CLASS_TYPE(WindowFocus)
			TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowLostFocusEvent : public Event
	{
	public:
		WindowLostFocusEvent() = default;

		TR_EVENT_CLASS_TYPE(WindowLostFocus)
			TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowMovedEvent : public Event
	{
	public:
		WindowMovedEvent(int32_t x, int32_t y) : m_X(x), m_Y(y) {}

		int32_t GetX() const { return m_X; }
		int32_t GetY() const { return m_Y; }

		std::string ToString() const override { return std::format("WindowMovedEvent: {}, {}", m_X, m_Y); }

		TR_EVENT_CLASS_TYPE(WindowMoved)
			TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		int32_t m_X = 0;
		int32_t m_Y = 0;
	};

	class WindowContentScaleEvent : public Event
	{
	public:
		explicit WindowContentScaleEvent(float scale) : m_Scale(scale) {}

		float GetScale() const { return m_Scale; }

		std::string ToString() const override { return std::format("WindowContentScaleEvent: {:.2f}", m_Scale); }

		TR_EVENT_CLASS_TYPE(WindowContentScale)
			TR_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		float m_Scale = 1.0f;
	};
}