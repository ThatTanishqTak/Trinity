#pragma once

#include "Trinity/Events/Event.hpp"
#include "Trinity/Input/GamepadCodes.hpp"

#include <format>

namespace Trinity
{
	class GamepadEvent : public Event
	{
	public:
		uint32_t GetGamepadId() const { return m_GamepadId; }

		TR_EVENT_CLASS_CATEGORY(EventCategoryGamepad | EventCategoryInput)

	protected:
		explicit GamepadEvent(uint32_t gamepadId) : m_GamepadId(gamepadId) {}

		uint32_t m_GamepadId = 0;
	};

	class GamepadConnectedEvent : public GamepadEvent
	{
	public:
		explicit GamepadConnectedEvent(uint32_t gamepadId) : GamepadEvent(gamepadId) {}

		std::string ToString() const override { return std::format("GamepadConnectedEvent: {}", m_GamepadId); }

		TR_EVENT_CLASS_TYPE(GamepadConnected)
	};

	class GamepadDisconnectedEvent : public GamepadEvent
	{
	public:
		explicit GamepadDisconnectedEvent(uint32_t gamepadId) : GamepadEvent(gamepadId) {}

		std::string ToString() const override { return std::format("GamepadDisconnectedEvent: {}", m_GamepadId); }

		TR_EVENT_CLASS_TYPE(GamepadDisconnected)
	};

	class GamepadButtonPressedEvent : public GamepadEvent
	{
	public:
		GamepadButtonPressedEvent(uint32_t gamepadId, GamepadButton button) : GamepadEvent(gamepadId), m_Button(button) {}

		GamepadButton GetButton() const { return m_Button; }

		std::string ToString() const override { return std::format("GamepadButtonPressedEvent: {} {}", m_GamepadId, GamepadButtonToString(m_Button)); }

		TR_EVENT_CLASS_TYPE(GamepadButtonPressed)

	private:
		GamepadButton m_Button;
	};

	class GamepadButtonReleasedEvent : public GamepadEvent
	{
	public:
		GamepadButtonReleasedEvent(uint32_t gamepadId, GamepadButton button) : GamepadEvent(gamepadId), m_Button(button) {}

		GamepadButton GetButton() const { return m_Button; }

		std::string ToString() const override { return std::format("GamepadButtonReleasedEvent: {} {}", m_GamepadId, GamepadButtonToString(m_Button)); }

		TR_EVENT_CLASS_TYPE(GamepadButtonReleased)

	private:
		GamepadButton m_Button;
	};

	class GamepadAxisMovedEvent : public GamepadEvent
	{
	public:
		GamepadAxisMovedEvent(uint32_t gamepadId, GamepadAxis axis, float value) : GamepadEvent(gamepadId), m_Axis(axis), m_Value(value) {}

		GamepadAxis GetAxis() const { return m_Axis; }
		float GetValue() const { return m_Value; }

		std::string ToString() const override { return std::format("GamepadAxisMovedEvent: {} {} = {:.3f}", m_GamepadId, GamepadAxisToString(m_Axis), m_Value); }

		TR_EVENT_CLASS_TYPE(GamepadAxisMoved)

	private:
		GamepadAxis m_Axis;
		float m_Value = 0.0f;
	};
}