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
		KeyCode GetKeyLabel() const { return m_KeyLabel; }

		TR_EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	protected:
		KeyEvent(KeyCode keyCode, KeyCode keyLabel) : m_KeyCode(keyCode), m_KeyLabel(keyLabel) {}

		KeyCode m_KeyCode;
		KeyCode m_KeyLabel;
	};

	class KeyPressedEvent : public KeyEvent
	{
	public:
		KeyPressedEvent(KeyCode keyCode, KeyCode keyLabel, bool isRepeat) : KeyEvent(keyCode, keyLabel), m_IsRepeat(isRepeat) {}

		bool IsRepeat() const { return m_IsRepeat; }

		std::string ToString() const override { return std::format("KeyPressedEvent: {} (label: {}, repeat: {})", KeyCodeToString(m_KeyCode), KeyCodeToString(m_KeyLabel), m_IsRepeat); }

		TR_EVENT_CLASS_TYPE(KeyPressed)

	private:
		bool m_IsRepeat = false;
	};

	class KeyReleasedEvent : public KeyEvent
	{
	public:
		KeyReleasedEvent(KeyCode keyCode, KeyCode keyLabel) : KeyEvent(keyCode, keyLabel) {}

		std::string ToString() const override { return std::format("KeyReleasedEvent: {} (label: {})", KeyCodeToString(m_KeyCode), KeyCodeToString(m_KeyLabel)); }

		TR_EVENT_CLASS_TYPE(KeyReleased)
	};

	class KeyTypedEvent : public Event
	{
	public:
		KeyTypedEvent(uint32_t codepoint) : m_Codepoint(codepoint) {}

		uint32_t GetCodepoint() const { return m_Codepoint; }

		std::string ToString() const override { return std::format("KeyTypedEvent: U+{:04X}", m_Codepoint); }

		TR_EVENT_CLASS_TYPE(KeyTyped)
			TR_EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	private:
		uint32_t m_Codepoint = 0;
	};
}