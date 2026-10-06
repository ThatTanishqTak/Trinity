#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/RHI/Device.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    class LayerStack;
    class TextureLoader;
    class Window;

    using OutputCallback = std::function<void(RHI::CommandList& commands, std::uint32_t width, std::uint32_t height)>;

    // Layers draw into a scene target, the size of the output unless SetSceneSize asks for another. A second pass copies it to the window, or to an offscreen target when there is no window, when the sizes match, and the UI is drawn over it
    class TRINITY_API Renderer
    {
    public:
        Renderer(RHI::Device& device, Window& window, std::string_view title);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        void RenderFrame(LayerStack& layers);

        [[nodiscard]] std::uint32_t AddOutput(Window& window, const std::array<float, 4>& clearColor, OutputCallback callback);
        void RemoveOutput(std::uint32_t output);

        void SetClearColor(const std::array<float, 4>& color) { m_ClearColor = color; }
        void SetTitle(std::string_view title);
        void SetSceneCopy(bool enabled) { m_SceneCopy = enabled; }
        void SetSceneSize(std::uint32_t width, std::uint32_t height);

        void SetVSync(bool enabled);
        [[nodiscard]] bool IsVSync() const;

        [[nodiscard]] RHI::Format GetSceneFormat() const;
        [[nodiscard]] RHI::Format GetOutputFormat() const;
        [[nodiscard]] RHI::TextureHandle GetSceneTarget() const { return m_SceneTarget; }
        [[nodiscard]] std::uint32_t GetSceneWidth() const { return m_SceneWidth; }
        [[nodiscard]] std::uint32_t GetSceneHeight() const { return m_SceneHeight; }
        [[nodiscard]] bool IsPresenting() const { return m_SwapChain != nullptr; }
        [[nodiscard]] std::uint64_t GetFrameCount() const { return m_FrameCount; }

    private:
        struct Output
        {
            std::uint32_t Id = 0;
            Window* Target = nullptr;
            Scope<RHI::SwapChain> SwapChain;
            RHI::TextureHandle Offscreen;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
            std::array<float, 4> ClearColor{};
            OutputCallback Callback;
        };

        void FollowWindow();
        void FollowOutputs();
        void CreateOffscreenTarget();
        void CreateSceneTarget();
        void CreateCopyPipeline();
        void RenderScene(RHI::CommandList& commands, LayerStack& layers);
        void RenderOutput(RHI::CommandList& commands, RHI::TextureHandle output, LayerStack& layers);
        void RenderAddedOutputs(RHI::CommandList& commands);
        void DestroyOutput(Output& output);
        [[nodiscard]] RHI::TextureHandle CreateOutputTarget(std::uint32_t width, std::uint32_t height, std::string_view debugName);
        void ReportFrameRate();

        [[nodiscard]] std::uint32_t GetOutputWidth() const;
        [[nodiscard]] std::uint32_t GetOutputHeight() const;
        [[nodiscard]] std::uint32_t GetWantedSceneWidth() const;
        [[nodiscard]] std::uint32_t GetWantedSceneHeight() const;

        RHI::Device& m_Device;
        Window& m_Window;
        std::string m_Title;
        Scope<TextureLoader> m_TextureLoader;

        Scope<RHI::SwapChain> m_SwapChain;
        RHI::TextureHandle m_OffscreenTarget;
        std::uint32_t m_TargetWidth = 0;
        std::uint32_t m_TargetHeight = 0;

        std::vector<Output> m_Outputs;
        std::uint32_t m_NextOutputId = 1;

        RHI::TextureHandle m_SceneTarget;
        RHI::ResourceState m_SceneState = RHI::ResourceState::Undefined;
        std::uint32_t m_SceneWidth = 0;
        std::uint32_t m_SceneHeight = 0;
        std::uint32_t m_RequestedSceneWidth = 0;
        std::uint32_t m_RequestedSceneHeight = 0;
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