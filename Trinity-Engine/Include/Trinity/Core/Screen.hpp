#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace Trinity
{
    // No exclusive fullscreen: borderless presents as fast through the flip model on Windows 10 and 11
    enum class FullScreenMode : std::uint8_t
    {
        Windowed,
        FullScreenWindow
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(FullScreenMode mode);
    [[nodiscard]] TRINITY_API std::optional<FullScreenMode> ParseFullScreenMode(std::string_view name);

    struct WindowSettings
    {
        static constexpr std::uint32_t c_MaxSize = 16384;

        FullScreenMode Mode = FullScreenMode::Windowed;
        std::uint32_t Width = 1280;
        std::uint32_t Height = 720;
        bool VSync = true;

        [[nodiscard]] bool operator==(const WindowSettings&) const = default;
    };

    // Empty parts keep what there is. A later request replaces one not yet applied
    struct ScreenRequest
    {
        std::optional<FullScreenMode> Mode;
        std::optional<std::uint32_t> Width;
        std::optional<std::uint32_t> Height;
        std::optional<bool> VSync;
    };

    // Set by whatever shows the game: a player's window, or Forge's Game panel
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

    // Changes apply once the frame ends. With no host, reads give 0 and changes warn. Main thread only
    class TRINITY_API Screen
    {
    public:
        [[nodiscard]] static std::uint32_t GetWidth();
        [[nodiscard]] static std::uint32_t GetHeight();
        [[nodiscard]] static FullScreenMode GetFullScreenMode();
        [[nodiscard]] static bool IsFullScreen();
        [[nodiscard]] static bool IsVSync();

        // The size applies while windowed. 0 or above WindowSettings::c_MaxSize is refused with a warning
        static void SetResolution(std::uint32_t width, std::uint32_t height, FullScreenMode mode);
        static void SetFullScreenMode(FullScreenMode mode);
        static void SetFullScreen(bool fullScreen);
        static void SetVSync(bool vsync);

        // Null for none
        static void SetHost(ScreenHost* host);
    };
}