#include "Trinity/Window/Window.hpp"

#include "Trinity/Core/Log.hpp"

#if defined(_WIN32)
#include "Trinity/Window/WindowsWindow.hpp"
#elif defined(__APPLE__)
#include "Trinity/Window/CocoaWindow.hpp"
#elif defined(__linux__)
#include "Trinity/Window/WaylandWindow.hpp"
#include "Trinity/Window/X11Window.hpp"

#include <cstdlib>
#include <string_view>
#endif

namespace Trinity
{
	std::unique_ptr<Window> Window::Create(const WindowSpecification& specification)
	{
		(void)specification;

#if defined(_WIN32)
		return std::make_unique<WindowsWindow>();
#elif defined(__APPLE__)
		return std::make_unique<CocoaWindow>();
#elif defined(__linux__)
		const char* l_Override = std::getenv("TRINITY_WINDOW_BACKEND");
		const bool l_ForceX11 = l_Override && std::string_view(l_Override) == "x11";

		if (!l_ForceX11 && std::getenv("WAYLAND_DISPLAY"))
		{
			TR_CORE_TRACE("Selected Wayland window backend");

			return std::make_unique<WaylandWindow>();
		}

		if (std::getenv("DISPLAY"))
		{
			TR_CORE_TRACE("Selected X11 window backend");

			return std::make_unique<X11Window>();
		}

		TR_CORE_CRITICAL("No display server found (WAYLAND_DISPLAY and DISPLAY are unset)");

		return nullptr;
#else
		TR_CORE_CRITICAL("No window backend for this platform");

		return nullptr;
#endif
	}
}