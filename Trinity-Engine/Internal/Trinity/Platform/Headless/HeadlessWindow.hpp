#pragma once

#include "Trinity/Core/Window.hpp"

#include <utility>

namespace Trinity
{
    class HeadlessWindow final : public Window
    {
    public:
        explicit HeadlessWindow(const WindowSpecification& specification);

        void PollEvents() override;

        [[nodiscard]] std::uint32_t GetWidth() const override { return m_Width; }
        [[nodiscard]] std::uint32_t GetHeight() const override { return m_Height; }

        void SetEventCallback(EventCallback callback) override { m_EventCallback = std::move(callback); }
        void SetRefreshCallback(RefreshCallback) override {}
        void SetTitle(std::string_view) override {}
        void SetCursorShape(CursorShape) override {}

        [[nodiscard]] WindowPosition GetPosition() const override { return m_Position; }
        void SetPosition(WindowPosition position) override;
        void SetSize(std::uint32_t width, std::uint32_t height) override;

        void Show(bool) override {}
        void Focus() override {}
        [[nodiscard]] bool IsFocused() const override { return false; }
        [[nodiscard]] bool IsMinimized() const override { return false; }

        void SetOpacity(float) override {}

        [[nodiscard]] float GetDpiScale() const override { return 1.0f; }

        [[nodiscard]] void* GetNativeHandle() const override { return nullptr; }

    private:
        void Dispatch(Event& event);

        EventCallback m_EventCallback;
        WindowPosition m_Position;
        std::uint32_t m_Width;
        std::uint32_t m_Height;
    };
}