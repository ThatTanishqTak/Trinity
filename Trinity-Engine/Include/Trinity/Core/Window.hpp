#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Events/Event.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

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

    // Width and Height size the client area. A window with an owner stays above it, and hides and closes with it
    struct WindowSpecification
    {
        std::string Title;
        std::uint32_t Width = 1600;
        std::uint32_t Height = 900;
        std::optional<WindowPosition> Position;
        // The monitor a game's window opens on, by the name the system gives it. A window that names one, or opens fullscreen, keeps its position only where that is on a monitor, and otherwise opens centred on the one named, or the primary when that one is not connected. Empty leaves a window with no position where the system places it
        std::string Monitor;
        // Borderless over the whole of its monitor, as SetFullscreen has it, from its first frame
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

        // Without its frame over the whole of the monitor it is on most, at the monitor's own size, which the flip model presents as fast as exclusive fullscreen, or back to the frame, place and size it had before
        virtual void SetFullscreen(bool fullscreen) = 0;
        [[nodiscard]] virtual bool IsFullscreen() const = 0;
        // The monitor the window is on most, by the name the system gives it, or empty where there are no monitors
        [[nodiscard]] virtual std::string GetMonitor() const = 0;

        virtual void Show(bool focus) = 0;
        virtual void Focus() = 0;
        [[nodiscard]] virtual bool IsFocused() const = 0;
        [[nodiscard]] virtual bool IsMinimized() const = 0;

        virtual void SetOpacity(float opacity) = 0;
        virtual void SetTopMost(bool topMost) = 0;
        virtual void SetFocusOnClick(bool focus) = 0;
        virtual void SetMousePassthrough(bool passthrough) = 0;

        [[nodiscard]] virtual float GetDpiScale() const = 0;

        [[nodiscard]] virtual void* GetNativeHandle() const = 0;

        [[nodiscard]] static Scope<Window> Create(const WindowSpecification& specification);
    };
}