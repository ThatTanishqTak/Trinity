#pragma once

#include <cstdint>

namespace Trinity
{
	struct NativeWindowHandle
	{
		enum class Platform : uint8_t { None, Win32, X11, Wayland, Cocoa };

		Platform Type = Platform::None;
		void* Display = nullptr;
		void* Surface = nullptr;
		uint64_t XWindow = 0;
	};
}