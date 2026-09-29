#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Trinity
{
	enum class KeyCode : uint16_t
	{
		Unknown = 0,

		TR_SPACE = 32,
		Apostrophe = 39,
		Comma = 44,
		Minus = 45,
		Period = 46,
		Slash = 47,

		D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,

		Semicolon = 59,
		Equal = 61,

		A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

		LeftBracket = 91,
		Backslash = 92,
		RightBracket = 93,
		GraveAccent = 96,

		World1 = 161,
		World2 = 162,

		Escape = 256, Enter, Tab, Backspace, Insert, Delete, Right, Left, Down, Up, PageUp, PageDown, Home, End,

		CapsLock = 280, ScrollLock, NumLock, PrintScreen, Pause,

		F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
		F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,

		KP0 = 320, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9,
		KPDecimal, KPDivide, KPMultiply, KPSubtract, KPAdd, KPEnter, KPEqual,

		LeftShift = 340, LeftControl, LeftAlt, LeftSuper, RightShift, RightControl, RightAlt, RightSuper, Menu
	};

	inline constexpr size_t KeyCodeCount = static_cast<size_t>(KeyCode::Menu) + 1;

	constexpr std::string_view KeyCodeToString(KeyCode keyCode)
	{
		constexpr std::string_view l_Digits = "0123456789";
		constexpr std::string_view l_Letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
		constexpr std::array<std::string_view, 24> l_FunctionKeys
		{
			"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
			"F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24"
		};
		constexpr std::array<std::string_view, 10> l_KeypadDigits
		{
			"KP0", "KP1", "KP2", "KP3", "KP4", "KP5", "KP6", "KP7", "KP8", "KP9"
		};

		const size_t l_Code = static_cast<size_t>(keyCode);

		if (keyCode >= KeyCode::D0 && keyCode <= KeyCode::D9)
		{
			return l_Digits.substr(l_Code - static_cast<size_t>(KeyCode::D0), 1);
		}

		if (keyCode >= KeyCode::A && keyCode <= KeyCode::Z)
		{
			return l_Letters.substr(l_Code - static_cast<size_t>(KeyCode::A), 1);
		}

		if (keyCode >= KeyCode::F1 && keyCode <= KeyCode::F24)
		{
			return l_FunctionKeys[l_Code - static_cast<size_t>(KeyCode::F1)];
		}

		if (keyCode >= KeyCode::KP0 && keyCode <= KeyCode::KP9)
		{
			return l_KeypadDigits[l_Code - static_cast<size_t>(KeyCode::KP0)];
		}

		switch (keyCode)
		{
			case KeyCode::TR_SPACE:
			{
				return "Space";
			}
			case KeyCode::Apostrophe:
			{
				return "Apostrophe";
			}
			case KeyCode::Comma:
			{
				return "Comma";
			}
			case KeyCode::Minus:
			{
				return "Minus";
			}
			case KeyCode::Period:
			{
				return "Period";
			}
			case KeyCode::Slash:
			{
				return "Slash";
			}
			case KeyCode::Semicolon:
			{
				return "Semicolon";
			}
			case KeyCode::Equal:
			{
				return "Equal";
			}
			case KeyCode::LeftBracket:
			{
				return "LeftBracket";
			}
			case KeyCode::Backslash:
			{
				return "Backslash";
			}
			case KeyCode::RightBracket:
			{
				return "RightBracket";
			}
			case KeyCode::GraveAccent:
			{
				return "GraveAccent";
			}
			case KeyCode::World1:
			{
				return "World1";
			}
			case KeyCode::World2:
			{
				return "World2";
			}
			case KeyCode::Escape:
			{
				return "Escape";
			}
			case KeyCode::Enter:
			{
				return "Enter";
			}
			case KeyCode::Tab:
			{
				return "Tab";
			}
			case KeyCode::Backspace:
			{
				return "Backspace";
			}
			case KeyCode::Insert:
			{
				return "Insert";
			}
			case KeyCode::Delete:
			{
				return "Delete";
			}
			case KeyCode::Right:
			{
				return "Right";
			}
			case KeyCode::Left:
			{
				return "Left";
			}
			case KeyCode::Down:
			{
				return "Down";
			}
			case KeyCode::Up:
			{
				return "Up";
			}
			case KeyCode::PageUp:
			{
				return "PageUp";
			}
			case KeyCode::PageDown:
			{
				return "PageDown";
			}
			case KeyCode::Home:
			{
				return "Home";
			}
			case KeyCode::End:
			{
				return "End";
			}
			case KeyCode::CapsLock:
			{
				return "CapsLock";
			}
			case KeyCode::ScrollLock:
			{
				return "ScrollLock";
			}
			case KeyCode::NumLock:
			{
				return "NumLock";
			}
			case KeyCode::PrintScreen:
			{
				return "PrintScreen";
			}
			case KeyCode::Pause:
			{
				return "Pause";
			}
			case KeyCode::KPDecimal:
			{
				return "KPDecimal";
			}
			case KeyCode::KPDivide:
			{
				return "KPDivide";
			}
			case KeyCode::KPMultiply:
			{
				return "KPMultiply";
			}
			case KeyCode::KPSubtract:
			{
				return "KPSubtract";
			}
			case KeyCode::KPAdd:
			{
				return "KPAdd";
			}
			case KeyCode::KPEnter:
			{
				return "KPEnter";
			}
			case KeyCode::KPEqual:
			{
				return "KPEqual";
			}
			case KeyCode::LeftShift:
			{
				return "LeftShift";
			}
			case KeyCode::LeftControl:
			{
				return "LeftControl";
			}
			case KeyCode::LeftAlt:
			{
				return "LeftAlt";
			}
			case KeyCode::LeftSuper:
			{
				return "LeftSuper";
			}
			case KeyCode::RightShift:
			{
				return "RightShift";
			}
			case KeyCode::RightControl:
			{
				return "RightControl";
			}
			case KeyCode::RightAlt:
			{
				return "RightAlt";
			}
			case KeyCode::RightSuper:
			{
				return "RightSuper";
			}
			case KeyCode::Menu:
			{
				return "Menu";
			}
			default:
			{
				return "Unknown";
			}
		}
	}
}