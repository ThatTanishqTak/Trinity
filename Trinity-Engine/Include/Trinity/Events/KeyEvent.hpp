#pragma once

#include "Trinity/Events/Event.hpp"
#include "Trinity/Input/KeyCodes.hpp"

#include <format>

namespace Trinity
{
	class KeyEvent : public Event
	{
	public:
		KeyCode GetKeyCode() const { return m_KeyCode; }

		TR_EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	protected:
		explicit KeyEvent(KeyCode keyCode) : m_KeyCode(keyCode) {}

		KeyCode m_KeyCode;
	};

	class KeyPressedEvent : public KeyEvent
	{
	public:
		KeyPressedEvent(KeyCode keyCode, bool isRepeat) : KeyEvent(keyCode), m_IsRepeat(isRepeat) {}

		bool IsRepeat() const { return m_IsRepeat; }

		std::string ToString() const override { return std::format("KeyPressedEvent: {} (repeat: {})", KeyCodeToString(m_KeyCode), m_IsRepeat); }

		TR_EVENT_CLASS_TYPE(KeyPressed)

	private:
		bool m_IsRepeat = false;
	};

	class KeyReleasedEvent : public KeyEvent
	{
	public:
		explicit KeyReleasedEvent(KeyCode keyCode) : KeyEvent(keyCode) {}

		std::string ToString() const override { return std::format("KeyReleasedEvent: {}", KeyCodeToString(m_KeyCode)); }

		TR_EVENT_CLASS_TYPE(KeyReleased)
	};

	class KeyTypedEvent : public Event
	{
	public:
		explicit KeyTypedEvent(uint32_t codepoint) : m_Codepoint(codepoint) {}

		uint32_t GetCodepoint() const { return m_Codepoint; }

		std::string ToString() const override { return std::format("KeyTypedEvent: U+{:04X}", m_Codepoint); }

		TR_EVENT_CLASS_TYPE(KeyTyped)
			TR_EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	private:
		uint32_t m_Codepoint = 0;
	};
}