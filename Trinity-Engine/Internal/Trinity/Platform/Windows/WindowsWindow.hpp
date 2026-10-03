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

        [[nodiscard]] void* GetNativeHandle() const override { return m_Handle; }

    private:
        static LRESULT CALLBACK WindowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        void Dispatch(Event& event);
        void Refresh();
        void OnMouseButton(MouseCode button, bool pressed);

        HWND m_Handle = nullptr;
        EventCallback m_EventCallback;
        RefreshCallback m_RefreshCallback;
        std::uint32_t m_Width = 0;
        std::uint32_t m_Height = 0;
        std::uint32_t m_MouseButtonsDown = 0;
        wchar_t m_HighSurrogate = 0;
        bool m_InSizeMove = false;
        bool m_Refreshing = false;
    };
}