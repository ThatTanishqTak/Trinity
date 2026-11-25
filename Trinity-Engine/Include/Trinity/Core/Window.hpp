#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Events/Event.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    enum class CursorShape : std::uint8_t
    {
        Arrow,
        TextInput,
        ResizeAll,
        ResizeNS,
        ResizeEW,
        ResizeNESW,
        ResizeNWSE,
        Hand,
        Wait,
        Progress,
        NotAllowed,
        Hidden
    };

    class Window;

    // The top left of a window's client area, in screen pixels
    struct WindowPosition
    {
        std::int32_t X = 0;
        std::int32_t Y = 0;

        [[nodiscard]] bool operator==(const WindowPosition&) const = default;
    };

    // A rectangle of a window's client area, in pixels from its top left
    struct WindowRect
    {
        std::int32_t Left = 0;
        std::int32_t Top = 0;
        std::int32_t Right = 0;
        std::int32_t Bottom = 0;

        [[nodiscard]] bool Contains(std::int32_t x, std::int32_t y) const { return x >= Left && x < Right && y >= Top && y < Bottom; }
        [[nodiscard]] bool operator==(const WindowRect&) const = default;
    };

    // What a point of a window with its own title bar is to the system: the application's, or the top edge the window is resized by, which the frame no longer holds
    enum class TitleBarHit : std::uint8_t
    {
        Client,
        Top,
        TopLeft,
        TopRight
    };

    // x and y are in client pixels. edge is how far in from the top the window is resized by, or 0 where it cannot be, as while it is maximised. Over a control, as a menu or a button of the title bar is, only a sliver of that resizes, so the control keeps nearly all of itself
    [[nodiscard]] TRINITY_API TitleBarHit HitTestTitleBar(std::span<const WindowRect> controls, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t edge);

    // Width and Height size the client area. An owned window stays above its owner, and closes with it
    struct WindowSpecification
    {
        std::string Title;
        std::uint32_t Width = 1600;
        std::uint32_t Height = 900;
        std::optional<WindowPosition> Position;
        // The monitor to open on, by the system's name. Empty leaves the placement to the system
        std::string Monitor;
        // Borderless over its whole monitor from the first frame
        bool Fullscreen = false;
        Window* Owner = nullptr;
        bool Resizable = true;
        bool Decorated = true;
        bool TaskbarIcon = true;
        bool TopMost = false;
        bool Visible = true;
        bool Headless = false;
    };

    class TRINITY_API Window
    {
    public:
        using EventCallback = std::function<void(Event&)>;
        using RefreshCallback = std::function<void()>;

        virtual ~Window() = default;

        virtual void PollEvents() = 0;

        [[nodiscard]] virtual std::uint32_t GetWidth() const = 0;
        [[nodiscard]] virtual std::uint32_t GetHeight() const = 0;

        virtual void SetEventCallback(EventCallback callback) = 0;
        virtual void SetRefreshCallback(RefreshCallback callback) = 0;
        virtual void SetTitle(std::string_view title) = 0;
        virtual void SetCursorShape(CursorShape shape) = 0;

        [[nodiscard]] virtual WindowPosition GetPosition() const = 0;
        virtual void SetPosition(WindowPosition position) = 0;
        virtual void SetSize(std::uint32_t width, std::uint32_t height) = 0;

        // Borderless over the monitor it is mostly on, or back to the frame, place and size it had
        virtual void SetFullscreen(bool fullscreen) = 0;
        [[nodiscard]] virtual bool IsFullscreen() const = 0;
        // The monitor the window is mostly on, or empty where there are none
        [[nodiscard]] virtual std::string GetMonitor() const = 0;

        virtual void Show(bool focus) = 0;
        virtual void Focus() = 0;
        [[nodiscard]] virtual bool IsFocused() const = 0;
        [[nodiscard]] virtual bool IsMinimized() const = 0;
        [[nodiscard]] virtual bool IsMaximized() const = 0;
        // Over the monitor's work area, or back to the place and size it had
        virtual void SetMaximized(bool maximized) = 0;
        virtual void Minimize() = 0;

        // The system's title bar given up, its place the client area's for the application to draw its own in. The frame keeps its shadow, its edges that resize and its snapping. True while the window has it so, which a window with no frame, or a platform that keeps its own title bar, never does
        virtual bool SetCustomTitleBar(bool custom) = 0;
        [[nodiscard]] virtual bool HasCustomTitleBar() const = 0;
        // The parts of the application's title bar that hold something to click, in client pixels, which keep all but a sliver of the top edge
        virtual void SetTitleBarControls(std::vector<WindowRect> controls) = 0;
        // Hands the window to the system to move, as a press on the system's own title bar does: it follows the mouse, whose left button is down on it, until the button is let go, and snaps as any window does. The press is ended for the application first
        virtual void BeginDrag() = 0;

        virtual void SetOpacity(float opacity) = 0;
        virtual void SetTopMost(bool topMost) = 0;
        virtual void SetFocusOnClick(bool focus) = 0;
        virtual void SetMousePassthrough(bool passthrough) = 0;
        // Whether files dragged from the system's file manager may be let go over it, which it tells of in a FilesDroppedEvent. An editor's window takes them, a game's does not
        virtual void SetAcceptsFiles(bool accepts) = 0;

        [[nodiscard]] virtual float GetDpiScale() const = 0;

        [[nodiscard]] virtual void* GetNativeHandle() const = 0;

        [[nodiscard]] static Scope<Window> Create(const WindowSpecification& specification);
    };
}