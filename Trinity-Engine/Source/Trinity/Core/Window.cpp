#include "Trinity/Core/Window.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Platform/Headless/HeadlessWindow.hpp"

#if defined(TR_PLATFORM_WINDOWS)
    #include "Trinity/Platform/Windows/WindowsWindow.hpp"
#endif

#include <algorithm>

namespace Trinity
{
    // The corners reach twice as far along the top edge as it is deep, as the system's do
    TitleBarHit HitTestTitleBar(std::span<const WindowRect> controls, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t edge)
    {
        const bool l_Control = std::ranges::any_of(controls, [x, y](const WindowRect& control) { return control.Contains(x, y); });
        if (y < 0 || y >= (l_Control ? (edge + 3) / 4 : edge))
        {
            return TitleBarHit::Client;
        }

        if (x < edge * 2)
        {
            return TitleBarHit::TopLeft;
        }

        return x >= width - edge * 2 ? TitleBarHit::TopRight : TitleBarHit::Top;
    }

    Scope<Window> Window::Create(const WindowSpecification& specification)
    {
        if (specification.Headless)
        {
            return CreateScope<HeadlessWindow>(specification);
        }
#if defined(TR_PLATFORM_WINDOWS)
        return CreateScope<WindowsWindow>(specification);
#else
        TR_CORE_WARN("No native window on this platform yet, running headless");
     
        return CreateScope<HeadlessWindow>(specification);
#endif
    }
}