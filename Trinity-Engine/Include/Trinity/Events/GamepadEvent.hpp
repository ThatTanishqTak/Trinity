#pragma once

#include "Trinity/Events/Event.hpp"

#include <cstdint>
#include <string_view>

namespace Trinity
{
    // A gamepad that came or went, by its slot in Input, and its name as the pad gives it, valid while the event is handled
    class TRINITY_API GamepadEvent : public Event
    {
    public:
        [[nodiscard]] std::uint32_t GetGamepad() const { return m_Gamepad; }
        [[nodiscard]] std::string_view GetDeviceName() const { return m_DeviceName; }

        TR_EVENT_CLASS_CATEGORY(EventCategoryGamepad | EventCategoryInput)

    protected:
        GamepadEvent(std::uint32_t gamepad, std::string_view deviceName) : m_Gamepad(gamepad), m_DeviceName(deviceName)
        {

        }

        std::uint32_t m_Gamepad;
        std::string_view m_DeviceName;
    };

    // Input already reads the pad as connected, released and centred
    class TRINITY_API GamepadConnectedEvent final : public GamepadEvent
    {
    public:
        GamepadConnectedEvent(std::uint32_t gamepad, std::string_view deviceName) : GamepadEvent(gamepad, deviceName)
        {

        }

        [[nodiscard]] std::string ToString() const override
        {
            return std::format("GamepadConnected: {} ({})", m_Gamepad, m_DeviceName);
        }

        TR_EVENT_CLASS_TYPE(GamepadConnected)
    };

    // Input already reads the slot as empty, with every button that was held released this frame
    class TRINITY_API GamepadDisconnectedEvent final : public GamepadEvent
    {
    public:
        GamepadDisconnectedEvent(std::uint32_t gamepad, std::string_view deviceName) : GamepadEvent(gamepad, deviceName)
        {

        }

        [[nodiscard]] std::string ToString() const override
        {
            return std::format("GamepadDisconnected: {} ({})", m_Gamepad, m_DeviceName);
        }

        TR_EVENT_CLASS_TYPE(GamepadDisconnected)
    };
}