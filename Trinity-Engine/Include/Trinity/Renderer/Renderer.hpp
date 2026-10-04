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
    class LayerStack;
    class Window;

    // Layers draw into a scene target the size of the output, which a second pass copies to the window, or to an offscreen target when there is no window, before the UI is drawn over it
    class TRINITY_API Renderer
    {
    public:
        Renderer(RHI::Device& device, Window& window, std::string_view title);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        void RenderFrame(LayerStack& layers);

        void SetClearColor(const std::array<float, 4>& color) { m_ClearColor = color; }
        void SetSceneCopy(bool enabled) { m_SceneCopy = enabled; }

        void SetVSync(bool enabled);
        [[nodiscard]] bool IsVSync() const;

        [[nodiscard]] RHI::Format GetSceneFormat() const;
        [[nodiscard]] RHI::Format GetOutputFormat() const;
        [[nodiscard]] RHI::TextureHandle GetSceneTarget() const { return m_SceneTarget; }
        [[nodiscard]] bool IsPresenting() const { return m_SwapChain != nullptr; }
        [[nodiscard]] std::uint64_t GetFrameCount() const { return m_FrameCount; }

    private:
        void FollowWindow();
        void CreateOffscreenTarget();
        void CreateSceneTarget();
        void CreateCopyPipeline();
        void RenderScene(RHI::CommandList& commands, LayerStack& layers);
        void RenderOutput(RHI::CommandList& commands, RHI::TextureHandle output, LayerStack& layers);
        void ReportFrameRate();

        [[nodiscard]] std::uint32_t GetOutputWidth() const;
        [[nodiscard]] std::uint32_t GetOutputHeight() const;

        RHI::Device& m_Device;
        Window& m_Window;
        std::string m_Title;

        Scope<RHI::SwapChain> m_SwapChain;
        RHI::TextureHandle m_OffscreenTarget;
        std::uint32_t m_TargetWidth = 0;
        std::uint32_t m_TargetHeight = 0;

        RHI::TextureHandle m_SceneTarget;
        RHI::ResourceState m_SceneState = RHI::ResourceState::Undefined;
        std::uint32_t m_SceneWidth = 0;
        std::uint32_t m_SceneHeight = 0;
        RHI::PipelineHandle m_CopyPipeline;
        bool m_SceneCopy = true;
        bool m_VSync = true;
        std::array<float, 4> m_ClearColor{ 0.1f, 0.1f, 0.12f, 1.0f };

        std::chrono::steady_clock::time_point m_StartTime;
        std::chrono::steady_clock::time_point m_ReportTime;
        std::uint64_t m_FrameCount = 0;
        std::uint32_t m_FramesSinceReport = 0;
    };
}