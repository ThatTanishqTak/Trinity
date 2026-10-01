#include "Trinity/Core/Window.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Platform/Headless/HeadlessWindow.hpp"

#if defined(TR_PLATFORM_WINDOWS)
    #include "Trinity/Platform/Windows/WindowsWindow.hpp"
#endif

namespace Trinity
{
    Scope<Window> Window::Create(const WindowSpecification& specification)
    {
        if (specification.Headless)
        {
            return CreateScope<HeadlessWindow>(specification);
        }
#if defined(TR_PLATFORM_WINDOWS)
        return CreateScope<WindowsWindow>(specification);
#else
        TR_CORE_WARN("No native window implementation for this platform yet - running headless.");
     
        return CreateScope<HeadlessWindow>(specification);
#endif
    }
}