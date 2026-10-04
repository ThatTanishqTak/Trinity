#pragma once

#include "Trinity/Core/Window.hpp"
#include "Trinity/Input/MouseCodes.hpp"
#include "Trinity/Platform/Windows/WindowsHeaders.hpp"

#include <utility>

namespace Trinity
{
    class WindowsWindow final : public Window
    {
    public:
        explicit WindowsWindow(const WindowSpecification& specification);
        ~WindowsWindow() override;

        WindowsWindow(const WindowsWindow&) = delete;
        WindowsWindow& operator=(const WindowsWindow&) = delete;

        void PollEvents() override;

        [[nodiscard]] std::uint32_t GetWidth() const override { return m_Width; }
        [[nodiscard]] std::uint32_t GetHeight() const override { return m_Height; }

        void SetEventCallback(EventCallback callback) override { m_EventCallback = std::move(callback); }
        void SetRefreshCallback(RefreshCallback callback) override { m_RefreshCallback = std::move(callback); }
        void SetTitle(std::string_view title) override;
        void SetCursorShape(CursorShape shape) override;

        [[nodiscard]] WindowPosition GetPosition() const override;
        void SetPosition(WindowPosition position) override;
        void SetSize(std::uint32_t width, std::uint32_t height) override;

        void Show(bool focus) override;
        void Focus() override;
        [[nodiscard]] bool IsFocused() const override;
        [[nodiscard]] bool IsMinimized() const override;

        void SetOpacity(float opacity) override;
        void SetTopMost(bool topMost) override;
        void SetFocusOnClick(bool focus) override { m_FocusOnClick = focus; }
        void SetMousePassthrough(bool passthrough) override { m_MousePassthrough = passthrough; }

        [[nodiscard]] float GetDpiScale() const override;

        [[nodiscard]] void* GetNativeHandle() const override { return m_Handle; }

    private:
        static LRESULT CALLBACK WindowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        void Dispatch(Event& event);
        void Refresh();
        void OnMouseButton(MouseCode button, bool pressed);
        void ApplyCursor() const;
        [[nodiscard]] RECT GetFrame(const RECT& client) const;

        HWND m_Handle = nullptr;
        EventCallback m_EventCallback;
        RefreshCallback m_RefreshCallback;
        std::uint32_t m_Width = 0;
        std::uint32_t m_Height = 0;
        std::uint32_t m_MouseButtonsDown = 0;
        wchar_t m_HighSurrogate = 0;
        CursorShape m_CursorShape = CursorShape::Arrow;
        bool m_InSizeMove = false;
        bool m_Refreshing = false;
        bool m_TrackingMouse = false;
        bool m_Counted = false;
        bool m_FocusOnClick = true;
        bool m_MousePassthrough = false;
    };
}