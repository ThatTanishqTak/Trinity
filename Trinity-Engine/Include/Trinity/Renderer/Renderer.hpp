#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Renderer/DebugDraw.hpp"
#include "Trinity/Renderer/FrameGraph.hpp"
#include "Trinity/Renderer/Renderer2D.hpp"
#include "Trinity/Renderer/Renderer3D.hpp"
#include "Trinity/Renderer/ToneMapping.hpp"
#include "Trinity/Renderer/WorldUI.hpp"
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

    // The main view, Renderer::c_MainView, always exists
    using ViewID = std::uint32_t;

    // Draws over each view's tone mapped image, as a game's UI does, in a pass after the debug lines
    class TRINITY_API ViewOverlay
    {
    public:
        enum class Needs : std::uint8_t
        {
            Nothing,
            Color,
            ColorAndStencil
        };

        virtual ~ViewOverlay() = default;

        virtual void RecordUploads(RHI::CommandList& commands) = 0;
        [[nodiscard]] virtual Needs GetNeeds(ViewID view) const = 0;
        // Inside the view's overlay pass, with a viewport and scissor covering the view
        virtual void Draw(RHI::CommandList& commands, ViewID view, std::uint32_t width, std::uint32_t height, bool stencil) = 0;
    };

    // entt::null where there is none
    struct PickResult
    {
        glm::uvec2 Pixel{ 0 };
        entt::entity Entity = entt::null;
    };

    // Reversed depth, so 0 is no surface, with the view-projection of the frame it was drawn in
    struct DepthPickResult
    {
        glm::uvec2 Pixel{ 0 };
        float Depth = 0.0f;
        glm::mat4 ViewProjection{ 1.0f };
    };

    // sRGB-encoded RGBA8 rows from the top
    struct ViewCapture
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::vector<std::byte> Pixels;
    };

    // Each frame is one frame graph that draws every view in turn, the main view first
    class TRINITY_API Renderer
    {
    public:
        // Where layers draw, and what the window shows
        static constexpr ViewID c_MainView = 0;
        static constexpr RHI::Format c_OverlayStencilFormat = RHI::Format::D32FloatS8;

        Renderer(RHI::Device& device, Window& window, std::string_view title);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        void RenderFrame(LayerStack& layers);

        [[nodiscard]] std::uint32_t AddOutput(Window& window, const std::array<float, 4>& clearColor, OutputCallback callback);
        void RemoveOutput(std::uint32_t output);

        // A size of 0 follows the output's, as the main view does. Call between frames
        [[nodiscard]] ViewID CreateView(std::uint32_t width = 0, std::uint32_t height = 0);
        // Between frames. The main view cannot be destroyed
        void DestroyView(ViewID view);
        [[nodiscard]] bool HasView(ViewID view) const;

        void SetClearColor(const std::array<float, 4>& color) { m_ClearColor = color; }
        // On the main thread, before the frame graph is built. The scene must outlive the frame
        void SubmitScene(Scene& scene, const RenderView& camera, const SceneOptions& options = {}, ViewID view = c_MainView);
        // Asked c_FramesInFlight frames ago, read without waiting on the GPU. Handed over once, in order
        [[nodiscard]] std::optional<PickResult> TakePickResult(ViewID view = c_MainView);
        // Read in the next frame that draws the scene's meshes. The last asked for wins
        void RequestDepthPick(glm::uvec2 pixel, ViewID view = c_MainView);
        [[nodiscard]] std::optional<DepthPickResult> TakeDepthPick(ViewID view = c_MainView);
        // Copied after everything drawn over the view, and handed back c_FramesInFlight frames later
        void RequestCapture(ViewID view = c_MainView);
        [[nodiscard]] std::optional<ViewCapture> TakeCapture(ViewID view = c_MainView);
        void SetTitle(std::string_view title);
        void SetSceneCopy(bool enabled) { m_SceneCopy = enabled; }
        void SetSceneSize(std::uint32_t width, std::uint32_t height, ViewID view = c_MainView);

        // Applies to every frame of the view until set again. Exposure 1 and no curve until then
        void SetToneMapping(const ToneMapping& toneMapping, ViewID view = c_MainView);
        [[nodiscard]] const ToneMapping& GetToneMapping(ViewID view = c_MainView) const;

        // Tone maps a linear scene texture into a display-format target of the same size
        void AddTonemapPass(FrameGraph& graph, FrameGraphTexture scene, FrameGraphTexture target, const ToneMapping& toneMapping) const;

        void SetVSync(bool enabled);
        [[nodiscard]] bool IsVSync() const;

        // One at a time, or none. It must outlive every frame it is set for
        void SetViewOverlay(ViewOverlay* overlay) { m_Overlay = overlay; }
        // One at a time, or none, for every view. It must outlive every frame it is set for
        void SetWorldUISource(WorldUISource* source);

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
        // With as many samples as the meshes' have, and 0 when they have none
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

        // A size of 0 follows the output. Each frame in flight has a pick readback slot
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
            // As picks: the pixel asked for next, and what each slot's frame asked, until its depth is read
            std::optional<glm::uvec2> DepthPixel;
            std::array<RHI::BufferHandle, RHI::c_FramesInFlight> DepthReadbacks{};
            std::array<std::optional<DepthPickResult>, RHI::c_FramesInFlight> DepthPicks{};
            std::optional<DepthPickResult> Depth;
            // As picks, for a capture of the display, with the size each slot's frame was copied at
            bool CaptureWanted = false;
            std::array<RHI::BufferHandle, RHI::c_FramesInFlight> CaptureReadbacks{};
            std::array<std::optional<glm::uvec2>, RHI::c_FramesInFlight> CaptureSizes{};
            std::optional<ViewCapture> Captured;
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
        void AddDepthPickPass(FrameGraph& graph, View& view, const Renderer3D::Passes& passes);
        void AddOverlayPass(FrameGraph& graph, const View& view, FrameGraphTexture display, ViewOverlay::Needs needs);
        void AddCapturePass(FrameGraph& graph, View& view, FrameGraphTexture display);
#if TR_DEBUG_DRAW
        void PrepareDebugLines();
        void AddDebugLinePass(FrameGraph& graph, const View& view, FrameGraphTexture display, const Renderer3D::Passes& passes);
#endif
        void ReadPick(View& view);
        void ReadDepthPick(View& view);
        void ReadCapture(View& view);
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
        ViewOverlay* m_Overlay = nullptr;
        WorldUISource* m_WorldUI = nullptr;

        Scope<RHI::SwapChain> m_SwapChain;
        RHI::TextureHandle m_OffscreenTarget;
        std::uint32_t m_TargetWidth = 0;
        std::uint32_t m_TargetHeight = 0;

        std::vector<Output> m_Outputs;
        std::uint32_t m_NextOutputId = 1;

        // The main view first. A view never moves in memory, since the graph's passes point at it
        std::vector<Scope<View>> m_Views;
        ViewID m_NextViewId = c_MainView + 1;
        // The display targets this frame's graph draws, which every pass that can show one reads
        std::vector<FrameGraphTexture, TaggedAllocator<FrameGraphTexture, MemoryTag::Renderer>> m_Displays;

        RHI::PipelineHandle m_CopyPipeline;
        RHI::PipelineHandle m_TonemapPipeline;
        RHI::PipelineHandle m_PickPipeline;
        RHI::PipelineHandle m_DepthPickPipeline;
        RHI::PipelineHandle m_OutlinePipeline;
        // Single-sampled, then with Renderer3D::c_SampleCount samples
        std::array<bool, 2> m_ReadableDepth{};
#if TR_DEBUG_DRAW
        // Depth-tested lines, then lines on top. The depth flags are single-sampled, then multisampled
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