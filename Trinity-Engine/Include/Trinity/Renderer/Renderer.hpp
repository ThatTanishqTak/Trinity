#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/RHI/Device.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace Trinity
{
    class Window;

    // Clears the window every frame, or a window-sized offscreen target when there is no window to present to
    class TRINITY_API Renderer
    {
    public:
        Renderer(RHI::Device& device, Window& window, std::string_view title);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        void RenderFrame();

        void SetClearColor(const std::array<float, 4>& color) { m_ClearColor = color; }

        [[nodiscard]] bool IsPresenting() const { return m_SwapChain != nullptr; }
        [[nodiscard]] std::uint64_t GetFrameCount() const { return m_FrameCount; }

    private:
        void ReportFrameRate();

        RHI::Device& m_Device;
        Window& m_Window;
        std::string m_Title;

        Scope<RHI::SwapChain> m_SwapChain;
        RHI::TextureHandle m_OffscreenTarget;
        std::array<float, 4> m_ClearColor{ 0.1f, 0.1f, 0.12f, 1.0f };

        std::chrono::steady_clock::time_point m_StartTime;
        std::chrono::steady_clock::time_point m_ReportTime;
        std::uint64_t m_FrameCount = 0;
        std::uint32_t m_FramesSinceReport = 0;
    };
}