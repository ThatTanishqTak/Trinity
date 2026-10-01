#pragma once

#include "Trinity/Events/Event.hpp"
#include "Trinity/Input/KeyCodes.hpp"

namespace Trinity
{
    class KeyEvent : public Event
    {
    public:
        [[nodiscard]] KeyCode GetKeyCode() const { return m_KeyCode; }

        TR_EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

    protected:
        explicit KeyEvent(KeyCode keyCode) : m_KeyCode(keyCode)
        {

        }

        KeyCode m_KeyCode;
    };

    class KeyPressedEvent final : public KeyEvent
    {
    public:
        KeyPressedEvent(KeyCode keyCode, bool isRepeat) : KeyEvent(keyCode), m_IsRepeat(isRepeat)
        {

        }

        [[nodiscard]] bool IsRepeat() const { return m_IsRepeat; }

        [[nodiscard]] std::string ToString() const override
        {
            return std::format("KeyPressed: {}{}", std::to_underlying(m_KeyCode), m_IsRepeat ? " (repeat)" : "");
        }

        TR_EVENT_CLASS_TYPE(KeyPressed)

    private:
        bool m_IsRepeat;
    };

    class KeyReleasedEvent final : public KeyEvent
    {
    public:
        explicit KeyReleasedEvent(KeyCode keyCode) : KeyEvent(keyCode)
        {

        }

        [[nodiscard]] std::string ToString() const override
        {
            return std::format("KeyReleased: {}", std::to_underlying(m_KeyCode));
        }

        TR_EVENT_CLASS_TYPE(KeyReleased)
    };

    class KeyTypedEvent final : public Event
    {
    public:
        explicit KeyTypedEvent(char32_t codepoint) : m_Codepoint(codepoint)
        {

        }

        [[nodiscard]] char32_t GetCodepoint() const { return m_Codepoint; }

        [[nodiscard]] std::string ToString() const override
        {
            return std::format("KeyTyped: U+{:04X}", static_cast<std::uint32_t>(m_Codepoint));
        }

        TR_EVENT_CLASS_TYPE(KeyTyped)
        TR_EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

    private:
        char32_t m_Codepoint;
    };
}