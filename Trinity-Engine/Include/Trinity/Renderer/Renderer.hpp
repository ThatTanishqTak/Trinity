#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Renderer/DebugDraw.hpp"
#include "Trinity/Renderer/FrameGraph.hpp"
#include "Trinity/Renderer/Renderer2D.hpp"
#include "Trinity/Renderer/Renderer3D.hpp"
#include "Trinity/Renderer/ToneMapping.hpp"
#include "Trinity/RHI/Device.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

    // Names one of the Renderer's views. The main view, Renderer::c_MainView, always exists
    using ViewID = std::uint32_t;

    // The entity drawn in a pixel SceneOptions::PickPixel asked about, entt::null where there is none
    struct PickResult
    {
        glm::uvec2 Pixel{ 0 };
        entt::entity Entity = entt::null;
    };

    // Each frame is a frame graph that draws every view in turn, the main view first. A view has scene and display targets of its own, a size, tone mapping, and a scene submitted to it with its options, ID targets and picks. A scene submitted for 3D is drawn first, by a depth pre-pass and an opaque pass. In the main view, layers then draw in linear light into an RGBA16Float scene target in the scene pass, the size of the output unless SetSceneSize asks for another, then add their own passes. The tonemap pass turns each view's scene into a display target of sRGB-encoded 8-bit values. The output pass copies the main view's to the window, or to an offscreen target when there is no window, when the sizes match, and the UI is drawn over it in gamma space, where it can show any view's display target. A scene that asks for entity IDs also has its sprites' drawn after the scene pass, a pixel picked from them and read back without waiting, and its selection outlined over the tonemapped image. Each added output gets a pass of its own
    class TRINITY_API Renderer
    {
    public:
        // Where layers draw, and what the window shows
        static constexpr ViewID c_MainView = 0;

        Renderer(RHI::Device& device, Window& window, std::string_view title);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        void RenderFrame(LayerStack& layers);

        [[nodiscard]] std::uint32_t AddOutput(Window& window, const std::array<float, 4>& clearColor, OutputCallback callback);
        void RemoveOutput(std::uint32_t output);

        // A view of this size, or one that follows the output's as the main view does when either is 0. Between frames. Its targets show the clear colour until a scene is first submitted to it, and keep what they last showed in a frame nothing is
        [[nodiscard]] ViewID CreateView(std::uint32_t width = 0, std::uint32_t height = 0);
        // Between frames. Its targets are released once no frame in flight can still use them. The main view cannot be destroyed
        void DestroyView(ViewID view);
        [[nodiscard]] bool HasView(ViewID view) const;

        void SetClearColor(const std::array<float, 4>& color) { m_ClearColor = color; }
        // Draws the scene's meshes into the view's scene target this frame, before anything else draws over them. On the main thread, after the transform pass and before the frame graph is built, as in OnPrepareRender. The scene must last until the frame is rendered
        void SubmitScene(Scene& scene, const RenderView& camera, const SceneOptions& options = {}, ViewID view = c_MainView);
        // The pick a view's scene asked for c_FramesInFlight frames ago, read without waiting on the GPU since that frame has finished, and handed over once. Picks come back in the order they were asked for
        [[nodiscard]] std::optional<PickResult> TakePickResult(ViewID view = c_MainView);
        void SetTitle(std::string_view title);
        void SetSceneCopy(bool enabled) { m_SceneCopy = enabled; }
        void SetSceneSize(std::uint32_t width, std::uint32_t height, ViewID view = c_MainView);

        // Applied to every frame of the view until set again, so a layer sets it from the camera it shows. Exposure 1 and no curve until then
        void SetToneMapping(const ToneMapping& toneMapping, ViewID view = c_MainView);
        [[nodiscard]] const ToneMapping& GetToneMapping(ViewID view = c_MainView) const;

        // A pass that tone maps a linear scene texture into a target of the display format, one texel per pixel, so the two are the same size. The Renderer adds one for each view it draws, and any graph can add more
        void AddTonemapPass(FrameGraph& graph, FrameGraphTexture scene, FrameGraphTexture target, const ToneMapping& toneMapping) const;

        void SetVSync(bool enabled);
        [[nodiscard]] bool IsVSync() const;

        [[nodiscard]] Renderer2D& GetRenderer2D() { return *m_Renderer2D; }
        [[nodiscard]] MaterialLoader& GetMaterialLoader() { return *m_MaterialLoader; }
        [[nodiscard]] Renderer3D& GetRenderer3D() { return *m_Renderer3D; }
        [[nodiscard]] const SceneDrawList& GetSceneDraws(ViewID view = c_MainView) const;
        [[nodiscard]] const FrameGraph& GetFrameGraph() const { return *m_FrameGraph; }
        [[nodiscard]] RHI::Format GetSceneFormat() const;
        [[nodiscard]] RHI::Format GetDisplayFormat() const;
        [[nodiscard]] RHI::Format GetOutputFormat() const;
        [[nodiscard]] RHI::TextureHandle GetSceneTarget(ViewID view = c_MainView) const;
        [[nodiscard]] RHI::TextureHandle GetDisplayTarget(ViewID view = c_MainView) const;
        [[nodiscard]] std::uint32_t GetSceneWidth(ViewID view = c_MainView) const;
        [[nodiscard]] std::uint32_t GetSceneHeight(ViewID view = c_MainView) const;
        [[nodiscard]] bool IsPresenting() const { return m_SwapChain != nullptr; }
        [[nodiscard]] std::uint64_t GetFrameCount() const { return m_FrameCount; }

    private:
        // The ID targets a scene's entities were drawn into, sprites' and meshes', with as many samples as the meshes' have, and 0 when they have none
        struct EntityTargets
        {
            FrameGraphTexture Sprites;
            FrameGraphTexture Meshes;
            std::uint32_t MeshSamples = 0;
        };

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

        // A view's targets, the size it asked for, where 0 follows the output, the scene submitted to it this frame, and a pick readback slot for each frame in flight, read when the slot comes round again, with the pixel its frame asked about, if any
        struct View
        {
            ViewID Id = c_MainView;
            std::uint32_t RequestedWidth = 0;
            std::uint32_t RequestedHeight = 0;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
            RHI::TextureHandle SceneTarget;
            RHI::TextureDescription SceneDescription;
            RHI::ResourceState SceneState = RHI::ResourceState::Undefined;
            RHI::TextureHandle DisplayTarget;
            RHI::TextureDescription DisplayDescription;
            RHI::ResourceState DisplayState = RHI::ResourceState::Undefined;
            ToneMapping Mapping{ c_NeutralEV100, Tonemapper::None };
            SceneDrawList Draws;
            SceneOptions Options;
            Scene* Submitted = nullptr;
            // Whether this frame's graph imported the targets, which it leaves as shader resources
            bool Imported = false;
            std::array<RHI::BufferHandle, RHI::c_FramesInFlight> PickReadbacks{};
            std::array<std::optional<glm::uvec2>, RHI::c_FramesInFlight> PickPixels{};
            std::optional<PickResult> Pick;
        };

        void FollowWindow();
        void FollowOutputs();
        void CreateOffscreenTarget();
        [[nodiscard]] ViewID AddView(ViewID id, std::uint32_t width, std::uint32_t height);
        void CreateViewTargets(View& view);
        void DestroyViewResources(View& view);
        [[nodiscard]] View* FindView(ViewID view) const;
        [[nodiscard]] RHI::PipelineHandle CreateFullscreenPipeline(std::string_view shader, RHI::Format format, std::string_view debugName, std::string_view consequence, bool alphaBlend = false, RHI::PrimitiveTopology topology = RHI::PrimitiveTopology::TriangleList);
        void CreatePicking();
        void BuildFrameGraph(LayerStack& layers);
        [[nodiscard]] FrameGraphTexture AddViewPasses(FrameGraph& graph, View& view, LayerStack* layers);
        [[nodiscard]] EntityTargets AddEntityPasses(FrameGraph& graph, View& view, const Renderer3D::Passes& passes);
        void AddOutlinePass(FrameGraph& graph, const View& view, const EntityTargets& targets, FrameGraphTexture display);
#if TR_DEBUG_DRAW
        void PrepareDebugLines();
        void AddDebugLinePass(FrameGraph& graph, const View& view, FrameGraphTexture display, const Renderer3D::Passes& passes);
#endif
        void ReadPick(View& view);
        void AddOutputPass(FrameGraphTexture display, RHI::TextureHandle output, LayerStack& layers);
        void AddAddedOutputPasses();
        void DestroyOutput(Output& output);
        [[nodiscard]] RHI::TextureHandle CreateOutputTarget(std::uint32_t width, std::uint32_t height, std::string_view debugName);
        void ReportFrameRate();

        [[nodiscard]] std::uint32_t GetOutputWidth() const;
        [[nodiscard]] std::uint32_t GetOutputHeight() const;
        [[nodiscard]] std::uint32_t GetWantedWidth(const View& view) const;
        [[nodiscard]] std::uint32_t GetWantedHeight(const View& view) const;

        RHI::Device& m_Device;
        Window& m_Window;
        std::string m_Title;
        Scope<TextureLoader> m_TextureLoader;
        Scope<MaterialLoader> m_MaterialLoader;
        Scope<MeshLoader> m_MeshLoader;
        Scope<EnvironmentLoader> m_EnvironmentLoader;
        Scope<Renderer2D> m_Renderer2D;
        Scope<Renderer3D> m_Renderer3D;
        Scope<FrameGraph> m_FrameGraph;

        Scope<RHI::SwapChain> m_SwapChain;
        RHI::TextureHandle m_OffscreenTarget;
        std::uint32_t m_TargetWidth = 0;
        std::uint32_t m_TargetHeight = 0;

        std::vector<Output> m_Outputs;
        std::uint32_t m_NextOutputId = 1;

        // The main view first. Each view stays where it is in memory while it lives, since the graph's passes point at it
        std::vector<Scope<View>> m_Views;
        ViewID m_NextViewId = c_MainView + 1;
        // The display targets this frame's graph draws, which every pass that can show one reads
        std::vector<FrameGraphTexture, TaggedAllocator<FrameGraphTexture, MemoryTag::Renderer>> m_Displays;

        RHI::PipelineHandle m_CopyPipeline;
        RHI::PipelineHandle m_TonemapPipeline;
        RHI::PipelineHandle m_PickPipeline;
        RHI::PipelineHandle m_OutlinePipeline;
#if TR_DEBUG_DRAW
        // Where this frame's debug lines are in the upload ring, depth-tested then on top, and whether the scene's depth can be read to test them, single-sampled then with Renderer3D::c_SampleCount samples
        struct DebugLines
        {
            std::uint32_t Buffer = RHI::c_NoBindlessIndex;
            std::uint32_t TestOffset = 0;
            std::uint32_t TestVertices = 0;
            std::uint32_t OnTopOffset = 0;
            std::uint32_t OnTopVertices = 0;
        };

        RHI::PipelineHandle m_DebugLinePipeline;
        DebugLines m_DebugLines;
        std::array<bool, 2> m_DebugDepthSupport{};
        bool m_ReportedNoDebugDepth = false;
#endif
        bool m_SceneCopy = true;
        bool m_VSync = true;
        std::array<float, 4> m_ClearColor{ 0.1f, 0.1f, 0.12f, 1.0f };

        std::chrono::steady_clock::time_point m_StartTime;
        std::chrono::steady_clock::time_point m_ReportTime;
        std::uint64_t m_FrameCount = 0;
        std::uint32_t m_FramesSinceReport = 0;
    };
}