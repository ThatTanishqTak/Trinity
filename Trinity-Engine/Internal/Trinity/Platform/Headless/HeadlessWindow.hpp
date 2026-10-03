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

        [[nodiscard]] void* GetNativeHandle() const override { return nullptr; }

    private:
        EventCallback m_EventCallback;
        std::uint32_t m_Width;
        std::uint32_t m_Height;
    };
}