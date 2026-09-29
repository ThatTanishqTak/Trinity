#pragma once

#include "Trinity/Events/Event.hpp"
#include "Trinity/Input/MouseCodes.hpp"

#include <format>

namespace Trinity
{
	class MouseMovedEvent : public Event
	{
	public:
		MouseMovedEvent(float x, float y) : m_X(x), m_Y(y) {}

		float GetX() const { return m_X; }
		float GetY() const { return m_Y; }

		std::string ToString() const override { return std::format("MouseMovedEvent: {}, {}", m_X, m_Y); }

		TR_EVENT_CLASS_TYPE(MouseMoved)
			TR_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

	private:
		float m_X = 0.0f;
		float m_Y = 0.0f;
	};

	class MouseScrolledEvent : public Event
	{
	public:
		MouseScrolledEvent(float xOffset, float yOffset) : m_XOffset(xOffset), m_YOffset(yOffset) {}

		float GetXOffset() const { return m_XOffset; }
		float GetYOffset() const { return m_YOffset; }

		std::string ToString() const override { return std::format("MouseScrolledEvent: {}, {}", m_XOffset, m_YOffset); }

		TR_EVENT_CLASS_TYPE(MouseScrolled)
			TR_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

	private:
		float m_XOffset = 0.0f;
		float m_YOffset = 0.0f;
	};

	class MouseButtonEvent : public Event
	{
	public:
		MouseCode GetMouseButton() const { return m_Button; }

		TR_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryMouseButton | EventCategoryInput)

	protected:
		explicit MouseButtonEvent(MouseCode button) : m_Button(button) {}

		MouseCode m_Button;
	};

	class MouseButtonPressedEvent : public MouseButtonEvent
	{
	public:
		explicit MouseButtonPressedEvent(MouseCode button) : MouseButtonEvent(button) {}

		std::string ToString() const override { return std::format("MouseButtonPressedEvent: {}", MouseCodeToString(m_Button)); }

		TR_EVENT_CLASS_TYPE(MouseButtonPressed)
	};

	class MouseButtonReleasedEvent : public MouseButtonEvent
	{
	public:
		explicit MouseButtonReleasedEvent(MouseCode button) : MouseButtonEvent(button) {}

		std::string ToString() const override { return std::format("MouseButtonReleasedEvent: {}", MouseCodeToString(m_Button)); }

		TR_EVENT_CLASS_TYPE(MouseButtonReleased)
	};
}