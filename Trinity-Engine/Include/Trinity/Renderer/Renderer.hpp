#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Renderer/FrameGraph.hpp"
#include "Trinity/Renderer/Renderer2D.hpp"
#include "Trinity/Renderer/Renderer3D.hpp"
#include "Trinity/Renderer/ToneMapping.hpp"
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
    class EnvironmentLoader;
    class LayerStack;
    class MaterialLoader;
    class MeshLoader;
    class TextureLoader;
    class Window;

    using OutputCallback = std::function<void(RHI::CommandList& commands, std::uint32_t width, std::uint32_t height)>;

    // Each frame is a frame graph. A scene submitted for 3D is drawn first, by a depth pre-pass and an opaque pass. Layers then draw in linear light into an RGBA16Float scene target in the scene pass, the size of the output unless SetSceneSize asks for another, then add their own passes. The tonemap pass turns the scene into a display target of sRGB-encoded 8-bit values, which the output pass copies to the window, or to an offscreen target when there is no window, when the sizes match, and the UI is drawn over it in gamma space. Each added output gets a pass of its own
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
        // Draws the scene's meshes into the scene target this frame, before layers draw over them. On the main thread, after the transform pass and before the frame graph is built, as in OnPrepareRender
        void SubmitScene(Scene& scene, const RenderView& view, const SceneOptions& options = {});
        void SetTitle(std::string_view title);
        void SetSceneCopy(bool enabled) { m_SceneCopy = enabled; }
        void SetSceneSize(std::uint32_t width, std::uint32_t height);

        // Applied to every frame until set again, so a layer sets it from the camera it shows. Exposure 1 and no curve until then
        void SetToneMapping(const ToneMapping& toneMapping) { m_ToneMapping = toneMapping; }
        [[nodiscard]] const ToneMapping& GetToneMapping() const { return m_ToneMapping; }

        // A pass that tone maps a linear scene texture into a target of the display format, one texel per pixel, so the two are the same size. The Renderer adds one each frame, and any graph can add more
        void AddTonemapPass(FrameGraph& graph, FrameGraphTexture scene, FrameGraphTexture target, const ToneMapping& toneMapping) const;

        void SetVSync(bool enabled);
        [[nodiscard]] bool IsVSync() const;

        [[nodiscard]] Renderer2D& GetRenderer2D() { return *m_Renderer2D; }
        [[nodiscard]] MaterialLoader& GetMaterialLoader() { return *m_MaterialLoader; }
        [[nodiscard]] Renderer3D& GetRenderer3D() { return *m_Renderer3D; }
        [[nodiscard]] const SceneDrawList& GetSceneDraws() const { return m_SceneDraws; }
        [[nodiscard]] const FrameGraph& GetFrameGraph() const { return *m_FrameGraph; }
        [[nodiscard]] RHI::Format GetSceneFormat() const;
        [[nodiscard]] RHI::Format GetDisplayFormat() const;
        [[nodiscard]] RHI::Format GetOutputFormat() const;
        [[nodiscard]] RHI::TextureHandle GetSceneTarget() const { return m_SceneTarget; }
        [[nodiscard]] RHI::TextureHandle GetDisplayTarget() const { return m_DisplayTarget; }
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
        [[nodiscard]] RHI::PipelineHandle CreateFullscreenPipeline(std::string_view shader, RHI::Format format, std::string_view debugName, std::string_view consequence);
        void BuildFrameGraph(LayerStack& layers);
        void AddOutputPass(FrameGraphTexture display, RHI::TextureHandle output, LayerStack& layers);
        void AddAddedOutputPasses(FrameGraphTexture display);
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
        Scope<MaterialLoader> m_MaterialLoader;
        Scope<MeshLoader> m_MeshLoader;
        Scope<EnvironmentLoader> m_EnvironmentLoader;
        Scope<Renderer2D> m_Renderer2D;
        Scope<Renderer3D> m_Renderer3D;
        SceneDrawList m_SceneDraws;
        SceneOptions m_SceneOptions;
        bool m_SceneSubmitted = false;
        Scope<FrameGraph> m_FrameGraph;

        Scope<RHI::SwapChain> m_SwapChain;
        RHI::TextureHandle m_OffscreenTarget;
        std::uint32_t m_TargetWidth = 0;
        std::uint32_t m_TargetHeight = 0;

        std::vector<Output> m_Outputs;
        std::uint32_t m_NextOutputId = 1;

        RHI::TextureHandle m_SceneTarget;
        RHI::TextureDescription m_SceneDescription;
        RHI::ResourceState m_SceneState = RHI::ResourceState::Undefined;
        RHI::TextureHandle m_DisplayTarget;
        RHI::TextureDescription m_DisplayDescription;
        RHI::ResourceState m_DisplayState = RHI::ResourceState::Undefined;
        std::uint32_t m_SceneWidth = 0;
        std::uint32_t m_SceneHeight = 0;
        std::uint32_t m_RequestedSceneWidth = 0;
        std::uint32_t m_RequestedSceneHeight = 0;
        RHI::PipelineHandle m_CopyPipeline;
        RHI::PipelineHandle m_TonemapPipeline;
        ToneMapping m_ToneMapping{ c_NeutralEV100, Tonemapper::None };
        bool m_SceneCopy = true;
        bool m_VSync = true;
        std::array<float, 4> m_ClearColor{ 0.1f, 0.1f, 0.12f, 1.0f };

        std::chrono::steady_clock::time_point m_StartTime;
        std::chrono::steady_clock::time_point m_ReportTime;
        std::uint64_t m_FrameCount = 0;
        std::uint32_t m_FramesSinceReport = 0;
    };
}