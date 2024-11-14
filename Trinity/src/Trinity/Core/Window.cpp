#include "trpch.h"
#include "Trinity/Core/Window.h"

#ifdef TR_PLATFORM_WINDOWS
#include "Platform/Windows/WindowsWindow.h"
#endif

namespace Trinity
{
	Scope<Window> Window::Create(const WindowProps& props)
	{
#ifdef TR_PLATFORM_WINDOWS
		return CreateScope<WindowsWindow>(props);
#else
		TR_CORE_ASSERT(false, "Unknown platform!");
		return nullptr;
#endif
	}

}