#include "Trinity/Input/Input.hpp"

#include "Trinity/Events/GamepadEvent.hpp"
#include "Trinity/Events/KeyEvent.hpp"
#include "Trinity/Events/MouseEvent.hpp"

#include <array>
#include <bitset>
#include <cstddef>

namespace Trinity
{
	namespace
	{
		constexpr size_t s_KeyCount = KeyCodeCount;
		constexpr size_t s_MouseButtonCount = static_cast<size_t>(MouseCode::Button4) + 1;
		constexpr size_t s_GamepadButtonCount = static_cast<size_t>(GamepadButton::Count);
		constexpr size_t s_GamepadAxisCount = static_cast<size_t>(GamepadAxis::Count);

		template<size_t N>
		struct ButtonState
		{
			std::bitset<N> Down;
			std::bitset<N> Pressed;
			std::bitset<N> Released;

			void Set(size_t index, bool down)
			{
				if (index >= N || Down.test(index) == down)
				{
					return;
				}

				Down.set(index, down);
				(down ? Pressed : Released).set(index);
			}

			bool IsDown(size_t index) const { return index < N && Down.test(index); }
			bool WasPressed(size_t index) const { return index < N && Pressed.test(index); }
			bool WasReleased(size_t index) const { return index < N && Released.test(index); }

			void ClearEdges()
			{
				Pressed.reset();
				Released.reset();
			}
		};

		struct GamepadState
		{
			bool Connected = false;
			ButtonState<s_GamepadButtonCount> Buttons;
			std::array<float, s_GamepadAxisCount> Axes{};
		};

		struct InputState
		{
			ButtonState<s_KeyCount> Keys;
			std::array<KeyCode, s_KeyCount> KeyLabels{};
			ButtonState<s_MouseButtonCount> MouseButtons;

			float MouseX = 0.0f;
			float MouseY = 0.0f;

			float ScrollX = 0.0f;
			float ScrollY = 0.0f;

			std::array<GamepadState, MaxGamepads> Gamepads{};
		};

		InputState s_State;

		GamepadState* FindGamepad(uint32_t gamepadId)
		{
			return gamepadId < MaxGamepads ? &s_State.Gamepads[gamepadId] : nullptr;
		}
	}

	bool Input::IsKeyDown(KeyCode key)
	{
		return s_State.Keys.IsDown(static_cast<size_t>(key));
	}

	bool Input::IsKeyPressed(KeyCode key)
	{
		return s_State.Keys.WasPressed(static_cast<size_t>(key));
	}

	bool Input::IsKeyReleased(KeyCode key)
	{
		return s_State.Keys.WasReleased(static_cast<size_t>(key));
	}

	bool Input::IsMouseButtonDown(MouseCode button)
	{
		return s_State.MouseButtons.IsDown(static_cast<size_t>(button));
	}

	bool Input::IsMouseButtonPressed(MouseCode button)
	{
		return s_State.MouseButtons.WasPressed(static_cast<size_t>(button));
	}

	bool Input::IsMouseButtonReleased(MouseCode button)
	{
		return s_State.MouseButtons.WasReleased(static_cast<size_t>(button));
	}

	std::pair<float, float> Input::GetMousePosition()
	{
		return { s_State.MouseX, s_State.MouseY };
	}

	std::pair<float, float> Input::GetScrollDelta()
	{
		return { s_State.ScrollX, s_State.ScrollY };
	}

	bool Input::IsGamepadConnected(uint32_t gamepadId)
	{
		const GamepadState* l_Gamepad = FindGamepad(gamepadId);

		return l_Gamepad && l_Gamepad->Connected;
	}

	bool Input::IsGamepadButtonDown(uint32_t gamepadId, GamepadButton button)
	{
		const GamepadState* l_Gamepad = FindGamepad(gamepadId);

		return l_Gamepad && l_Gamepad->Buttons.IsDown(static_cast<size_t>(button));
	}

	bool Input::IsGamepadButtonPressed(uint32_t gamepadId, GamepadButton button)
	{
		const GamepadState* l_Gamepad = FindGamepad(gamepadId);

		return l_Gamepad && l_Gamepad->Buttons.WasPressed(static_cast<size_t>(button));
	}

	bool Input::IsGamepadButtonReleased(uint32_t gamepadId, GamepadButton button)
	{
		const GamepadState* l_Gamepad = FindGamepad(gamepadId);

		return l_Gamepad && l_Gamepad->Buttons.WasReleased(static_cast<size_t>(button));
	}

	float Input::GetGamepadAxis(uint32_t gamepadId, GamepadAxis axis)
	{
		const GamepadState* l_Gamepad = FindGamepad(gamepadId);
		const size_t l_Index = static_cast<size_t>(axis);

		return (l_Gamepad && l_Index < s_GamepadAxisCount) ? l_Gamepad->Axes[l_Index] : 0.0f;
	}

	void Input::OnEvent(const Event& event)
	{
		switch (event.GetEventType())
		{
			case EventType::KeyPressed:
			case EventType::KeyReleased:
			{
				const auto& l_Event = static_cast<const KeyEvent&>(event);
				const size_t l_Index = static_cast<size_t>(l_Event.GetKeyCode());
				s_State.Keys.Set(l_Index, event.GetEventType() == EventType::KeyPressed);

				if (l_Index < s_KeyCount && event.GetEventType() == EventType::KeyPressed)
				{
					s_State.KeyLabels[l_Index] = l_Event.GetKeyLabel();
				}

				break;
			}
			case EventType::MouseButtonPressed:
			case EventType::MouseButtonReleased:
			{
				const auto& l_Event = static_cast<const MouseButtonEvent&>(event);
				s_State.MouseButtons.Set(static_cast<size_t>(l_Event.GetMouseButton()), event.GetEventType() == EventType::MouseButtonPressed);

				break;
			}
			case EventType::MouseMoved:
			{
				const auto& l_Event = static_cast<const MouseMovedEvent&>(event);
				s_State.MouseX = l_Event.GetX();
				s_State.MouseY = l_Event.GetY();

				break;
			}
			case EventType::MouseScrolled:
			{
				const auto& l_Event = static_cast<const MouseScrolledEvent&>(event);
				s_State.ScrollX += l_Event.GetXOffset();
				s_State.ScrollY += l_Event.GetYOffset();

				break;
			}
			case EventType::GamepadConnected:
			case EventType::GamepadDisconnected:
			{
				const auto& l_Event = static_cast<const GamepadEvent&>(event);
				if (GamepadState* l_Gamepad = FindGamepad(l_Event.GetGamepadId()))
				{
					l_Gamepad->Connected = event.GetEventType() == EventType::GamepadConnected;
					l_Gamepad->Buttons.Down.reset();
					l_Gamepad->Axes = {};
				}

				break;
			}
			case EventType::GamepadButtonPressed:
			{
				const auto& l_Event = static_cast<const GamepadButtonPressedEvent&>(event);
				if (GamepadState* l_Gamepad = FindGamepad(l_Event.GetGamepadId()))
				{
					l_Gamepad->Buttons.Set(static_cast<size_t>(l_Event.GetButton()), true);
				}

				break;
			}
			case EventType::GamepadButtonReleased:
			{
				const auto& l_Event = static_cast<const GamepadButtonReleasedEvent&>(event);
				if (GamepadState* l_Gamepad = FindGamepad(l_Event.GetGamepadId()))
				{
					l_Gamepad->Buttons.Set(static_cast<size_t>(l_Event.GetButton()), false);
				}

				break;
			}
			case EventType::GamepadAxisMoved:
			{
				const auto& l_Event = static_cast<const GamepadAxisMovedEvent&>(event);
				const size_t l_Index = static_cast<size_t>(l_Event.GetAxis());

				GamepadState* l_Gamepad = FindGamepad(l_Event.GetGamepadId());
				if (l_Gamepad && l_Index < s_GamepadAxisCount)
				{
					l_Gamepad->Axes[l_Index] = l_Event.GetValue();
				}

				break;
			}
			default:
			{
				break;
			}
		}
	}

	void Input::EndFrame()
	{
		s_State.Keys.ClearEdges();
		s_State.MouseButtons.ClearEdges();

		for (GamepadState& l_Gamepad : s_State.Gamepads)
		{
			l_Gamepad.Buttons.ClearEdges();
		}

		s_State.ScrollX = 0.0f;
		s_State.ScrollY = 0.0f;
	}

	std::vector<KeyCode> Input::GetHeldKeys()
	{
		std::vector<KeyCode> l_Keys;
		for (size_t l_Index = 0; l_Index < s_KeyCount; ++l_Index)
		{
			if (s_State.Keys.IsDown(l_Index))
			{
				l_Keys.push_back(static_cast<KeyCode>(l_Index));
			}
		}

		return l_Keys;
	}

	KeyCode Input::GetHeldKeyLabel(KeyCode key)
	{
		const size_t l_Index = static_cast<size_t>(key);

		return l_Index < s_KeyCount ? s_State.KeyLabels[l_Index] : KeyCode::Unknown;
	}

	std::vector<MouseCode> Input::GetHeldMouseButtons()
	{
		std::vector<MouseCode> l_Buttons;
		for (size_t l_Index = 0; l_Index < s_MouseButtonCount; ++l_Index)
		{
			if (s_State.MouseButtons.IsDown(l_Index))
			{
				l_Buttons.push_back(static_cast<MouseCode>(l_Index));
			}
		}

		return l_Buttons;
	}
}