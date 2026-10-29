#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace Trinity
{
    // How a game's window shows it, named as Unity's FullScreenMode names them: framed at its own size, or without a frame over the whole of its monitor, at the monitor's own size. Trinity has no exclusive fullscreen, since borderless presents as fast through the flip model on Windows 10 and 11
    enum class FullScreenMode : std::uint8_t
    {
        Windowed,
        FullScreenWindow
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(FullScreenMode mode);
    [[nodiscard]] TRINITY_API std::optional<FullScreenMode> ParseFullScreenMode(std::string_view name);

    // A game's window: its mode, the size of its client area in pixels while it is windowed, and whether each frame waits for the display's vertical blank
    struct WindowSettings
    {
        static constexpr std::uint32_t c_MaxSize = 16384;

        FullScreenMode Mode = FullScreenMode::Windowed;
        std::uint32_t Width = 1280;
        std::uint32_t Height = 720;
        bool VSync = true;

        [[nodiscard]] bool operator==(const WindowSettings&) const = default;
    };

    // A change to the screen a script asks for. Each part left empty keeps what there is, and one asked for again before the change applies replaces the first
    struct ScreenRequest
    {
        std::optional<FullScreenMode> Mode;
        std::optional<std::uint32_t> Width;
        std::optional<std::uint32_t> Height;
        std::optional<bool> VSync;
    };

    // What Screen reads and changes, set by whatever shows the game: a player's own window, or Forge's Game panel, which ignores changes
    class TRINITY_API ScreenHost
    {
    public:
        virtual ~ScreenHost() = default;

        // What the game draws at, in pixels
        [[nodiscard]] virtual std::uint32_t GetWidth() const = 0;
        [[nodiscard]] virtual std::uint32_t GetHeight() const = 0;
        [[nodiscard]] virtual FullScreenMode GetFullScreenMode() const = 0;
        [[nodiscard]] virtual bool IsVSync() const = 0;
        virtual void Request(const ScreenRequest& request) = 0;
    };

    // The game's screen, as Unity's Screen has it: the size the game draws at, its mode and vsync. A change applies once the frame ends, as Unity's do, so reads give the screen as it is until then. A player keeps the change as its player's choice, and Forge's Game panel ignores it. With no host, as in a program that shows no game, everything reads as 0 and off, and a change is ignored with one warning. Main thread only
    class TRINITY_API Screen
    {
    public:
        [[nodiscard]] static std::uint32_t GetWidth();
        [[nodiscard]] static std::uint32_t GetHeight();
        [[nodiscard]] static FullScreenMode GetFullScreenMode();
        [[nodiscard]] static bool IsFullScreen();
        [[nodiscard]] static bool IsVSync();

        // The mode, and the window's size while windowed. In FullScreenWindow the game draws at the monitor's size, and the size is the window's when it goes back. A size of 0, or above WindowSettings::c_MaxSize, is refused with a warning
        static void SetResolution(std::uint32_t width, std::uint32_t height, FullScreenMode mode);
        static void SetFullScreenMode(FullScreenMode mode);
        static void SetFullScreen(bool fullScreen);
        static void SetVSync(bool vsync);

        // Null for none
        static void SetHost(ScreenHost* host);
    };
}