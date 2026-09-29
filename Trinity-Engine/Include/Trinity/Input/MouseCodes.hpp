#pragma once

#include <cstdint>
#include <string_view>

namespace Trinity
{
	enum class MouseCode : uint8_t
	{
		Left = 0,
		Right = 1,
		Middle = 2,
		Button3 = 3,
		Button4 = 4
	};

	constexpr std::string_view MouseCodeToString(MouseCode button)
	{
		switch (button)
		{
			case MouseCode::Left:
			{
				return "Left";
			}
			case MouseCode::Right:
			{
				return "Right";
			}
			case MouseCode::Middle:
			{
				return "Middle";
			}
			case MouseCode::Button3:
			{
				return "Button3";
			}
			case MouseCode::Button4:
			{
				return "Button4";
			}
		}

		return "Unknown";
	}
}