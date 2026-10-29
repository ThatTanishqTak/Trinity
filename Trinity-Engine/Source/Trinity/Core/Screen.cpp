#include "Trinity/Core/Screen.hpp"

#include "Trinity/Core/Log.hpp"

#include <utility>

namespace Trinity
{
    namespace
    {
        ScreenHost* s_Host = nullptr;
        bool s_WarnedNoHost = false;

        void Request(const ScreenRequest& request)
        {
            if (s_Host != nullptr)
            {
                s_Host->Request(request);

                return;
            }

            if (!std::exchange(s_WarnedNoHost, true))
            {
                TR_CORE_WARN("Screen: nothing here shows a game, so a change to its resolution, full screen mode or vsync is ignored");
            }
        }
    }

    std::string_view ToString(FullScreenMode mode)
    {
        switch (mode)
        {
            case FullScreenMode::Windowed:
            {
                return "Windowed";
            }
            case FullScreenMode::FullScreenWindow:
            {
                return "FullScreenWindow";
            }
        }

        return "Unknown";
    }

    std::optional<FullScreenMode> ParseFullScreenMode(std::string_view name)
    {
        for (const FullScreenMode it_Mode : { FullScreenMode::Windowed, FullScreenMode::FullScreenWindow })
        {
            if (ToString(it_Mode) == name)
            {
                return it_Mode;
            }
        }

        return std::nullopt;
    }

    std::uint32_t Screen::GetWidth()
    {
        return s_Host != nullptr ? s_Host->GetWidth() : 0;
    }

    std::uint32_t Screen::GetHeight()
    {
        return s_Host != nullptr ? s_Host->GetHeight() : 0;
    }

    FullScreenMode Screen::GetFullScreenMode()
    {
        return s_Host != nullptr ? s_Host->GetFullScreenMode() : FullScreenMode::Windowed;
    }

    bool Screen::IsFullScreen()
    {
        return GetFullScreenMode() != FullScreenMode::Windowed;
    }

    bool Screen::IsVSync()
    {
        return s_Host != nullptr && s_Host->IsVSync();
    }

    void Screen::SetResolution(std::uint32_t width, std::uint32_t height, FullScreenMode mode)
    {
        if (width == 0 || height == 0 || width > WindowSettings::c_MaxSize || height > WindowSettings::c_MaxSize)
        {
            TR_CORE_WARN("Screen: {}x{} is no size a window can have, so the resolution is not changed", width, height);

            return;
        }

        ScreenRequest l_Request;
        l_Request.Mode = mode;
        l_Request.Width = width;
        l_Request.Height = height;
        Request(l_Request);
    }

    void Screen::SetFullScreenMode(FullScreenMode mode)
    {
        ScreenRequest l_Request;
        l_Request.Mode = mode;
        Request(l_Request);
    }

    void Screen::SetFullScreen(bool fullScreen)
    {
        SetFullScreenMode(fullScreen ? FullScreenMode::FullScreenWindow : FullScreenMode::Windowed);
    }

    void Screen::SetVSync(bool vsync)
    {
        ScreenRequest l_Request;
        l_Request.VSync = vsync;
        Request(l_Request);
    }

    void Screen::SetHost(ScreenHost* host)
    {
        s_Host = host;
        s_WarnedNoHost = false;
    }
}