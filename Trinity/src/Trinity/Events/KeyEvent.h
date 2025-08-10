#pragma once

#include "Trinity/Events/Event.h"

namespace Trinity
{
    class KeyEvent : public Event
    {
    public:
        int GetKeyCode() const { return m_KeyCode; }

        TR_EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

    protected:
        KeyEvent(int keycode) : m_KeyCode(keycode) {}

        int m_KeyCode;
    };

    class KeyPressedEvent : public KeyEvent
    {
    public:
        KeyPressedEvent(int keycode, int repeatCount) : KeyEvent(keycode), m_RepeatCount(repeatCount)
        {

        }

        int GetRepeatCount() const { return m_RepeatCount; }

        std::string ToString() const override
        {
            return FormatEvent("KeyPressedEvent: ", m_KeyCode, " (", m_RepeatCount, " repeats)");
        }

        TR_EVENT_CLASS_TYPE(KeyPressed)

    private:
        int m_RepeatCount;
    };

    class KeyReleasedEvent : public KeyEvent
    {
    public:
        KeyReleasedEvent(int keycode) : KeyEvent(keycode)
        {

        }

        std::string ToString() const override
        {
            return FormatEvent("KeyReleasedEvent: ", m_KeyCode);
        }

        TR_EVENT_CLASS_TYPE(KeyReleased)
    };
}