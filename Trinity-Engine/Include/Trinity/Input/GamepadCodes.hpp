#pragma once

#include <cstdint>
#include <string_view>

namespace Trinity
{
	inline constexpr uint32_t MaxGamepads = 8;

	enum class GamepadButton : uint8_t
	{
		South = 0,
		East,
		West,
		North,
		LeftShoulder,
		RightShoulder,
		Back,
		Start,
		LeftStick,
		RightStick,
		DPadUp,
		DPadRight,
		DPadDown,
		DPadLeft,
		Count
	};

	enum class GamepadAxis : uint8_t
	{
		LeftX = 0,
		LeftY,
		RightX,
		RightY,
		LeftTrigger,
		RightTrigger,
		Count
	};

	constexpr std::string_view GamepadButtonToString(GamepadButton button)
	{
		switch (button)
		{
			case GamepadButton::South:
			{
				return "South";
			}
			case GamepadButton::East:
			{
				return "East";
			}
			case GamepadButton::West:
			{
				return "West";
			}
			case GamepadButton::North:
			{
				return "North";
			}
			case GamepadButton::LeftShoulder:
			{
				return "LeftShoulder";
			}
			case GamepadButton::RightShoulder:
			{
				return "RightShoulder";
			}
			case GamepadButton::Back:
			{
				return "Back";
			}
			case GamepadButton::Start:
			{
				return "Start";
			}
			case GamepadButton::LeftStick:
			{
				return "LeftStick";
			}
			case GamepadButton::RightStick:
			{
				return "RightStick";
			}
			case GamepadButton::DPadUp:
			{
				return "DPadUp";
			}
			case GamepadButton::DPadRight:
			{
				return "DPadRight";
			}
			case GamepadButton::DPadDown:
			{
				return "DPadDown";
			}
			case GamepadButton::DPadLeft:
			{
				return "DPadLeft";
			}
			case GamepadButton::Count:
			{
				break;
			}
		}

		return "Unknown";
	}

	constexpr std::string_view GamepadAxisToString(GamepadAxis axis)
	{
		switch (axis)
		{
			case GamepadAxis::LeftX:
			{
				return "LeftX";
			}
			case GamepadAxis::LeftY:
			{
				return "LeftY";
			}
			case GamepadAxis::RightX:
			{
				return "RightX";
			}
			case GamepadAxis::RightY:
			{
				return "RightY";
			}
			case GamepadAxis::LeftTrigger:
			{
				return "LeftTrigger";
			}
			case GamepadAxis::RightTrigger:
			{
				return "RightTrigger";
			}
			case GamepadAxis::Count:
			{
				break;
			}
		}

		return "Unknown";
	}
}