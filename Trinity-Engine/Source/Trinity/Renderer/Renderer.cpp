#include "Trinity/Renderer/Renderer.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/EnvironmentLoader.hpp"
#include "Trinity/Asset/MaterialLoader.hpp"
#include "Trinity/Asset/MeshLoader.hpp"
#include "Trinity/Asset/TextureLoader.hpp"
#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/LayerStack.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Core/Window.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        ConsoleVariable<bool> s_VSyncVariable("renderer.vsync", true, "Waits for the display's vertical blank before showing each frame");

        constexpr RHI::Format c_SceneFormat = RHI::Format::RGBA16Float;
        constexpr RHI::Format c_DisplayFormat = RHI::Format::RGBA8Unorm;

        // Laid out as Tonemap.slang reads it
        struct TonemapPushData
        {
            std::array<std::uint32_t, 2> Scene{};
            float Exposure = 1.0f;
            std::uint32_t Curve = 0;
        };

        static_assert(sizeof(TonemapPushData) == 16);

        // Laid out as EntityPick.slang reads it
        struct PickPushData
        {
            std::uint32_t Sprites = 0;
            std::uint32_t Meshes = 0;
            std::uint32_t MeshSamples = 0;
            std::uint32_t Padding = 0;
            std::array<std::int32_t, 2> Pixel{};
            std::array<std::uint32_t, 2> Output{};
        };

        static_assert(sizeof(PickPushData) == 32);

        // Laid out as SelectionOutline.slang reads it
        struct OutlinePushData
        {
            std::uint32_t Sprites = 0;
            std::uint32_t Meshes = 0;
            std::uint32_t MeshSamples = 0;
            std::uint32_t SelectedCount = 0;
            std::array<std::uint32_t, 2> Selected{};
            std::uint32_t SelectedOffset = 0;
            std::uint32_t Padding = 0;
            std::array<std::int32_t, 2> Size{};
            std::array<std::int32_t, 2> Padding2{};
            std::array<float, 4> Color{};
        };

        static_assert(sizeof(OutlinePushData) == 64);

        // One entity ID, in a buffer of the size a copy is safe with on every backend
        constexpr std::uint64_t c_PickSize = 16;
        // The orange an editor marks its selection with, sRGB-encoded as the display target is
        constexpr std::array<float, 4> c_OutlineColor{ 1.0f, 0.627f, 0.157f, 1.0f };

        // What the graph is told about a window's texture or an offscreen target it imports, of which it only reads the size
        RHI::TextureDescription GetOutputDescription(std::uint32_t width, std::uint32_t height, RHI::Format format)
        {
            RHI::TextureDescription l_Description;
            l_Description.Width = width;
            l_Description.Height = height;
            l_Description.TextureFormat = format;
            l_Description.Usage = RHI::TextureUsage::RenderTarget;
            l_Description.OptimizedClear = false;

            return l_Description;
        }
    }

    Renderer::Renderer(RHI::Device& device, Window& window, std::string_view title) : m_Device(device), m_Window(window), m_Title(title)
    {
        const RHI::DeviceInfo& l_Info = m_Device.GetInfo();
        m_TargetWidth = m_Window.GetWidth();
        m_TargetHeight = m_Window.GetHeight();
        m_VSync = s_VSyncVariable.Get();

        if (m_Window.GetNativeHandle() != nullptr)
        {
            RHI::SwapChainSpecification l_Specification;
            l_Specification.NativeWindow = m_Window.GetNativeHandle();
            l_Specification.Width = m_TargetWidth;
            l_Specification.Height = m_TargetHeight;
            l_Specification.VSync = m_VSync;

            m_SwapChain = m_Device.CreateSwapChain(l_Specification);
            if (!m_SwapChain)
            {
                TR_CORE_ERROR("Renderer: {} cannot present to the window, so frames go to an offscreen target instead", ToString(l_Info.API));
            }
        }

        if (!m_SwapChain)
        {
            CreateOffscreenTarget();
        }

        static_cast<void>(AddView(c_MainView, 0, 0));
        m_CopyPipeline = CreateFullscreenPipeline("SceneCopy", GetOutputFormat(), "Renderer scene copy", "the output only shows the clear colour");
        m_TonemapPipeline = CreateFullscreenPipeline("Tonemap", c_DisplayFormat, "Renderer tonemap", "the scene is shown as black");
        CreatePicking();

        // Textures, materials, meshes and environments need this device, so their loaders live exactly as long as the renderer. Materials keep textures loaded, so they go before textures do
        m_TextureLoader = CreateScope<TextureLoader>(m_Device);
        AssetManager::RegisterLoader(*m_TextureLoader);
        m_MaterialLoader = CreateScope<MaterialLoader>(m_Device);
        AssetManager::RegisterLoader(*m_MaterialLoader);
        m_MeshLoader = CreateScope<MeshLoader>(m_Device);
        AssetManager::RegisterLoader(*m_MeshLoader);
        m_EnvironmentLoader = CreateScope<EnvironmentLoader>(m_Device);
        AssetManager::RegisterLoader(*m_EnvironmentLoader);

        // Sprites without a texture, or whose texture is still loading, draw with the loader's white placeholder
        const Asset* l_White = m_TextureLoader->GetPlaceholder();
        m_Renderer2D = CreateScope<Renderer2D>(m_Device, l_White != nullptr ? static_cast<const TextureAsset*>(l_White)->GetShaderResourceIndex() : RHI::c_NoBindlessIndex);
        m_Renderer3D = CreateScope<Renderer3D>(m_Device, *m_MaterialLoader);
        m_FrameGraph = CreateScope<FrameGraph>(m_Device);

        m_StartTime = std::chrono::steady_clock::now();
        m_ReportTime = m_StartTime;

        TR_CORE_INFO("Renderer: {} on {}, drawing to {} through a {} scene target tone mapped into {}", ToString(l_Info.API), l_Info.AdapterName, m_SwapChain ? std::format("the window ({}x{})", m_SwapChain->GetWidth(), m_SwapChain->GetHeight()) : std::format("an offscreen {}x{} target", m_Window.GetWidth(), m_Window.GetHeight()), RHI::ToString(c_SceneFormat), RHI::ToString(c_DisplayFormat));
    }

    Renderer::~Renderer()
    {
        const double l_Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_StartTime).count();
        TR_CORE_INFO("Renderer: {} frame(s) on {} at an average of {:.1f} fps", m_FrameCount, ToString(m_Device.GetInfo().API), l_Seconds > 0.0 ? static_cast<double>(m_FrameCount) / l_Seconds : 0.0);

        for (Output& it_Output : m_Outputs)
        {
            DestroyOutput(it_Output);
        }

        m_FrameGraph.reset();
        for (const Scope<View>& it_View : m_Views)
        {
            it_View->Draws.Clear();
        }

        m_Renderer3D.reset();
        m_Renderer2D.reset();
        AssetManager::UnregisterLoader(EnvironmentAsset::c_AssetType);
        m_EnvironmentLoader.reset();
        AssetManager::UnregisterLoader(MeshAsset::c_AssetType);
        m_MeshLoader.reset();
        AssetManager::UnregisterLoader(MaterialAsset::c_AssetType);
        m_MaterialLoader.reset();
        AssetManager::UnregisterLoader(TextureAsset::c_AssetType);
        m_TextureLoader.reset();

        m_Outputs.clear();
        m_SwapChain.reset();
        m_Device.DestroyPipeline(m_TonemapPipeline);
        m_Device.DestroyPipeline(m_PickPipeline);
        m_Device.DestroyPipeline(m_OutlinePipeline);
        for (const Scope<View>& it_View : m_Views)
        {
            DestroyViewResources(*it_View);
        }

        m_Views.clear();
        m_Device.DestroyPipeline(m_CopyPipeline);
        m_Device.DestroyTexture(m_OffscreenTarget);
        m_Device.WaitIdle();
    }

    void Renderer::RenderFrame(LayerStack& layers)
    {
        TR_PROFILE_FUNCTION();

        if (m_Window.GetWidth() == 0 || m_Window.GetHeight() == 0)
        {
            return;
        }

        FollowWindow();

        RHI::CommandList& l_Commands = m_Device.BeginFrame();
        for (const Scope<View>& it_View : m_Views)
        {
            ReadPick(*it_View);
        }

        m_TextureLoader->RecordUploads(l_Commands);
        m_MaterialLoader->RecordUploads(l_Commands);
        m_MeshLoader->RecordUploads(l_Commands);
        m_EnvironmentLoader->RecordUploads(l_Commands);
        m_Renderer2D->BeginFrame();
        m_Renderer3D->BeginFrame();

        {
            TR_PROFILE_SCOPE("LayerStack::OnPrepareRender");
            for (const Scope<Layer>& it_Layer : layers)
            {
                it_Layer->OnPrepareRender(l_Commands);
            }
        }

        BuildFrameGraph(layers);
        m_FrameGraph->Execute(l_Commands);
        for (const Scope<View>& it_View : m_Views)
        {
            View& l_View = *it_View;
            if (std::exchange(l_View.Imported, false))
            {
                l_View.SceneState = RHI::ResourceState::ShaderResource;
                l_View.DisplayState = l_View.DisplayTarget ? RHI::ResourceState::ShaderResource : l_View.DisplayState;
            }

            l_View.Submitted = nullptr;
        }

        m_Device.EndFrame();

        if (m_SwapChain)
        {
            m_SwapChain->Present();
        }

        for (Output& it_Output : m_Outputs)
        {
            if (it_Output.SwapChain)
            {
                it_Output.SwapChain->Present();
            }
        }

        ++m_FrameCount;
        ++m_FramesSinceReport;
        ReportFrameRate();
    }

    // Every view in turn, the main view first, then the output pass and each added output, which read every display target drawn this frame
    void Renderer::BuildFrameGraph(LayerStack& layers)
    {
        TR_PROFILE_FUNCTION();

        FrameGraph& l_Graph = *m_FrameGraph;
        l_Graph.Reset();
        m_Displays.clear();

        FrameGraphTexture l_MainDisplay;
        for (const Scope<View>& it_View : m_Views)
        {
            const bool l_Main = it_View->Id == c_MainView;
            const FrameGraphTexture l_Display = AddViewPasses(l_Graph, *it_View, l_Main ? &layers : nullptr);
            l_MainDisplay = l_Main ? l_Display : l_MainDisplay;
            if (l_Display)
            {
                m_Displays.push_back(l_Display);
            }
        }

        const RHI::TextureHandle l_Output = m_SwapChain ? m_SwapChain->AcquireNextTexture() : m_OffscreenTarget;
        if (l_Output)
        {
            AddOutputPass(l_MainDisplay, l_Output, layers);
        }

        AddAddedOutputPasses();
    }

    // The targets are imported in whatever state the last frame left them, and both end as shader resources. The scene is cleared to the linear clear colour, by the opaque pass when a scene was submitted. Layers draw into the main view alone, which is drawn every frame. Another view is drawn when a scene was submitted to it, or cleared while its targets have never been drawn, and otherwise keeps what it last showed
    FrameGraphTexture Renderer::AddViewPasses(FrameGraph& graph, View& view, LayerStack* layers)
    {
        const bool l_Submitted = view.Submitted != nullptr;
        const bool l_Draw = view.SceneTarget && (layers != nullptr || l_Submitted || view.DisplayState == RHI::ResourceState::Undefined);

        FrameGraphTexture l_Scene;
        Renderer3D::Passes l_Passes;
        if (l_Draw)
        {
            const std::array<float, 4> l_Clear{ SrgbToLinear(m_ClearColor[0]), SrgbToLinear(m_ClearColor[1]), SrgbToLinear(m_ClearColor[2]), m_ClearColor[3] };
            l_Scene = graph.ImportTexture("Scene", view.SceneTarget, view.SceneDescription, view.SceneState, RHI::ResourceState::ShaderResource);
            view.Imported = true;
            if (l_Submitted)
            {
                l_Passes = m_Renderer3D->AddPasses(graph, view.Draws, l_Scene, view.SceneDescription, l_Clear, view.Options);
            }

            // The opaque pass has already cleared a submitted scene, which then needs this pass only for its sprites or the layers
            const bool l_Sprites = l_Submitted && view.Options.Sprites;
            if (layers != nullptr || l_Sprites || !l_Submitted)
            {
                const RHI::LoadOp l_Load = l_Submitted ? RHI::LoadOp::Load : RHI::LoadOp::Clear;
                View* l_View = &view;
                graph.AddPass("Scene", FrameGraphPassType::Raster, [l_Scene, l_Clear, l_Load](FrameGraphPassBuilder& builder) { builder.AddColorAttachment({ l_Scene, l_Load, l_Clear }); }, [this, l_View, l_Sprites, layers](const FrameGraphContext& context)
                {
                    if (l_Sprites)
                    {
                        m_Renderer2D->DrawScene(context.GetCommands(), *l_View->Submitted, l_View->Draws.View.ViewProjection, l_View->SceneDescription.TextureFormat, l_View->SceneDescription.Width, l_View->SceneDescription.Height);
                    }

                    if (layers != nullptr)
                    {
                        TR_PROFILE_SCOPE("LayerStack::OnRender");
                        for (const Scope<Layer>& it_Layer : *layers)
                        {
                            it_Layer->OnRender(context.GetCommands());
                        }
                    }
                });
            }
        }

        const EntityTargets l_Entities = l_Scene && l_Submitted && view.Options.EntityIDs ? AddEntityPasses(graph, view, l_Passes) : EntityTargets{};

        if (layers != nullptr)
        {
            TR_PROFILE_SCOPE("LayerStack::OnBuildFrameGraph");
            for (const Scope<Layer>& it_Layer : *layers)
            {
                it_Layer->OnBuildFrameGraph(graph, l_Scene);
            }
        }

        FrameGraphTexture l_Display;
        if (l_Scene && view.DisplayTarget)
        {
            l_Display = graph.ImportTexture("Display", view.DisplayTarget, view.DisplayDescription, view.DisplayState, RHI::ResourceState::ShaderResource);
            AddTonemapPass(graph, l_Scene, l_Display, view.Mapping);
            if (l_Entities.Sprites)
            {
                AddOutlinePass(graph, view, l_Entities, l_Display);
            }
        }

        return l_Display;
    }

    void Renderer::SubmitScene(Scene& scene, const RenderView& camera, const SceneOptions& options, ViewID view)
    {
        View* l_View = FindView(view);
        if (l_View == nullptr)
        {
            return;
        }

        m_Renderer3D->Collect(scene, camera, l_View->Draws);
        l_View->Options = options;
        l_View->Submitted = &scene;
    }

    // The sprites' entities, through the view the scene was submitted with, into a target of their own, since they are drawn into the single-sampled scene after the meshes. Then, when a pixel is asked about, its entity is copied into this frame's readback slot of the view. Nothing reads the sprites' target when nothing is picked or outlined, so the graph culls its pass
    Renderer::EntityTargets Renderer::AddEntityPasses(FrameGraph& graph, View& view, const Renderer3D::Passes& passes)
    {
        const std::uint32_t l_Width = view.SceneDescription.Width;
        const std::uint32_t l_Height = view.SceneDescription.Height;

        EntityTargets l_Targets;
        l_Targets.Meshes = passes.EntityIDs;
        l_Targets.MeshSamples = passes.EntityIDs ? passes.SampleCount : 0;

        RHI::TextureDescription l_Description;
        l_Description.Width = l_Width;
        l_Description.Height = l_Height;
        l_Description.TextureFormat = Renderer3D::c_EntityIDFormat;
        l_Description.ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        l_Description.DebugName = "Sprite entity IDs";
        l_Targets.Sprites = graph.CreateTexture("Sprite entity IDs", l_Description);

        const FrameGraphTexture l_Sprites = l_Targets.Sprites;
        Scene* l_Scene = view.Submitted;
        const glm::mat4 l_ViewProjection = view.Draws.View.ViewProjection;
        graph.AddPass("Sprite entity IDs", FrameGraphPassType::Raster, [l_Sprites](FrameGraphPassBuilder& builder)
        {
            builder.AddColorAttachment({ l_Sprites, RHI::LoadOp::Clear, { 0.0f, 0.0f, 0.0f, 0.0f } });
        }, [this, l_Scene, l_ViewProjection, l_Width, l_Height](const FrameGraphContext& context)
        {
            m_Renderer2D->DrawSceneIDs(context.GetCommands(), *l_Scene, l_ViewProjection, l_Width, l_Height);
        });

        const std::size_t l_Slot = static_cast<std::size_t>(m_FrameCount % RHI::c_FramesInFlight);
        const std::optional<glm::uvec2> l_Pixel = view.Options.PickPixel;
        if (!l_Pixel || l_Pixel->x >= l_Width || l_Pixel->y >= l_Height || !m_PickPipeline || !view.PickReadbacks[l_Slot])
        {
            return l_Targets;
        }

        view.PickPixels[l_Slot] = *l_Pixel;
        const FrameGraphTexture l_Meshes = l_Targets.Meshes;
        const std::uint32_t l_MeshSamples = l_Targets.MeshSamples;
        const FrameGraphBuffer l_Picked = graph.CreateBuffer("Picked entity", c_PickSize);
        const FrameGraphBuffer l_Readback = graph.ImportBuffer("Picked entity readback", view.PickReadbacks[l_Slot], c_PickSize, RHI::ResourceState::CopyDestination, RHI::ResourceState::CopyDestination);
        const RHI::PipelineHandle l_Pipeline = m_PickPipeline;
        graph.AddPass("Entity pick", FrameGraphPassType::Compute, [l_Sprites, l_Meshes, l_Picked](FrameGraphPassBuilder& builder)
        {
            builder.Read(l_Sprites, RHI::ResourceState::ShaderResource);
            if (l_Meshes)
            {
                builder.Read(l_Meshes, RHI::ResourceState::ShaderResource);
            }

            builder.Write(l_Picked, RHI::ResourceState::UnorderedAccess);
        }, [l_Sprites, l_Meshes, l_MeshSamples, l_Picked, l_Pipeline, l_Pixel](const FrameGraphContext& context)
        {
            PickPushData l_Push;
            l_Push.Sprites = context.GetDevice().GetShaderResourceIndex(context.GetTexture(l_Sprites));
            l_Push.Meshes = l_Meshes ? context.GetDevice().GetShaderResourceIndex(context.GetTexture(l_Meshes)) : 0;
            l_Push.MeshSamples = l_MeshSamples;
            l_Push.Pixel = { static_cast<std::int32_t>(l_Pixel->x), static_cast<std::int32_t>(l_Pixel->y) };
            l_Push.Output = { context.GetDevice().GetUnorderedAccessIndex(context.GetBuffer(l_Picked)), 0 };

            context.GetCommands().SetPipeline(l_Pipeline);
            context.GetCommands().PushConstants(std::as_bytes(std::span(&l_Push, 1)));
            context.GetCommands().Dispatch(1, 1, 1);
        });

        graph.AddPass("Entity pick readback", FrameGraphPassType::Copy, [l_Picked, l_Readback](FrameGraphPassBuilder& builder)
        {
            builder.Read(l_Picked, RHI::ResourceState::CopySource);
            builder.Write(l_Readback, RHI::ResourceState::CopyDestination);
            builder.SetSideEffect();
        }, [l_Picked, l_Readback](const FrameGraphContext& context)
        {
            context.GetCommands().CopyBuffer(context.GetBuffer(l_Picked), 0, context.GetBuffer(l_Readback), 0, c_PickSize);
        });

        return l_Targets;
    }

    // Around the entities the view's scene asked to outline, over the tonemapped image and under the UI. Their IDs go into the upload ring sorted, for the shader's binary search
    void Renderer::AddOutlinePass(FrameGraph& graph, const View& view, const EntityTargets& targets, FrameGraphTexture display)
    {
        std::vector<std::uint32_t> l_Outlined(view.Options.Outlined.begin(), view.Options.Outlined.end());
        std::erase(l_Outlined, ToPickID(entt::null));
        std::ranges::sort(l_Outlined);
        l_Outlined.erase(std::ranges::unique(l_Outlined).begin(), l_Outlined.end());
        if (l_Outlined.empty() || !m_OutlinePipeline)
        {
            return;
        }

        const RHI::UploadAllocation l_Upload = m_Device.AllocateUpload(l_Outlined.size() * sizeof(std::uint32_t), 16);
        if (l_Upload.Data.empty() || l_Upload.ShaderResourceIndex == RHI::c_NoBindlessIndex)
        {
            TR_CORE_ERROR("Renderer: no upload memory for {} outlined entities, so the selection is not outlined this frame", l_Outlined.size());

            return;
        }

        std::memcpy(l_Upload.Data.data(), l_Outlined.data(), l_Outlined.size() * sizeof(std::uint32_t));

        OutlinePushData l_Push;
        l_Push.MeshSamples = targets.MeshSamples;
        l_Push.SelectedCount = static_cast<std::uint32_t>(l_Outlined.size());
        l_Push.Selected = { l_Upload.ShaderResourceIndex, 0 };
        l_Push.SelectedOffset = static_cast<std::uint32_t>(l_Upload.Offset);
        l_Push.Size = { static_cast<std::int32_t>(view.SceneDescription.Width), static_cast<std::int32_t>(view.SceneDescription.Height) };
        l_Push.Color = c_OutlineColor;

        const FrameGraphTexture l_Sprites = targets.Sprites;
        const FrameGraphTexture l_Meshes = targets.Meshes;
        const RHI::PipelineHandle l_Pipeline = m_OutlinePipeline;
        graph.AddPass("Selection outline", FrameGraphPassType::Raster, [display, l_Sprites, l_Meshes](FrameGraphPassBuilder& builder)
        {
            builder.AddColorAttachment({ display, RHI::LoadOp::Load });
            builder.Read(l_Sprites, RHI::ResourceState::ShaderResource);
            if (l_Meshes)
            {
                builder.Read(l_Meshes, RHI::ResourceState::ShaderResource);
            }
        }, [l_Push, l_Sprites, l_Meshes, l_Pipeline](const FrameGraphContext& context)
        {
            OutlinePushData l_Data = l_Push;
            l_Data.Sprites = context.GetDevice().GetShaderResourceIndex(context.GetTexture(l_Sprites));
            l_Data.Meshes = l_Meshes ? context.GetDevice().GetShaderResourceIndex(context.GetTexture(l_Meshes)) : 0;

            context.GetCommands().SetPipeline(l_Pipeline);
            context.GetCommands().PushConstants(std::as_bytes(std::span(&l_Data, 1)));
            context.GetCommands().Draw(3, 1, 0, 0);
        });
    }

    // What the frame that last used the view's slot asked about. That frame is c_FramesInFlight behind, and BeginFrame has waited for it, so its copy is done and reading it never stalls
    void Renderer::ReadPick(View& view)
    {
        const std::size_t l_Slot = static_cast<std::size_t>(m_FrameCount % RHI::c_FramesInFlight);
        const std::optional<glm::uvec2> l_Pixel = std::exchange(view.PickPixels[l_Slot], std::nullopt);
        if (!l_Pixel)
        {
            return;
        }

        const std::span<const std::byte> l_Data = m_Device.GetMappedData(view.PickReadbacks[l_Slot]);
        if (l_Data.size() < sizeof(std::uint32_t))
        {
            return;
        }

        std::uint32_t l_ID = 0;
        std::memcpy(&l_ID, l_Data.data(), sizeof(l_ID));
        view.Pick = PickResult{ *l_Pixel, FromPickID(l_ID) };
    }

    std::optional<PickResult> Renderer::TakePickResult(ViewID view)
    {
        View* l_View = FindView(view);

        return l_View != nullptr ? std::exchange(l_View->Pick, std::nullopt) : std::nullopt;
    }

    void Renderer::AddTonemapPass(FrameGraph& graph, FrameGraphTexture scene, FrameGraphTexture target, const ToneMapping& toneMapping) const
    {
        const RHI::PipelineHandle l_Pipeline = m_TonemapPipeline;
        const float l_Exposure = GetExposureScale(toneMapping.ExposureEV100);
        const std::uint32_t l_Curve = static_cast<std::uint32_t>(toneMapping.Curve);
        graph.AddPass("Tonemap", FrameGraphPassType::Raster, [scene, target, l_Pipeline](FrameGraphPassBuilder& builder)
        {
            builder.AddColorAttachment({ target, l_Pipeline ? RHI::LoadOp::DontCare : RHI::LoadOp::Clear });
            builder.Read(scene, RHI::ResourceState::ShaderResource);
        }, [scene, l_Pipeline, l_Exposure, l_Curve](const FrameGraphContext& context)
        {
            if (!l_Pipeline)
            {
                return;
            }

            TonemapPushData l_PushData;
            l_PushData.Scene = { context.GetDevice().GetShaderResourceIndex(context.GetTexture(scene)), 0 };
            l_PushData.Exposure = l_Exposure;
            l_PushData.Curve = l_Curve;

            context.GetCommands().SetPipeline(l_Pipeline);
            context.GetCommands().PushConstants(std::as_bytes(std::span(&l_PushData, 1)));
            context.GetCommands().Draw(3, 1, 0, 0);
        });
    }

    // The copy reads one main view display texel per output pixel, so the tone mapped scene reaches the output exactly as written, and the UI goes over it, showing any view's display target
    void Renderer::AddOutputPass(FrameGraphTexture display, RHI::TextureHandle output, LayerStack& layers)
    {
        const std::uint32_t l_Width = GetOutputWidth();
        const std::uint32_t l_Height = GetOutputHeight();

        // A swap chain recreated while acquiring can differ from the display target for one frame, which then shows the clear colour
        const View& l_Main = *m_Views.front();
        const bool l_Copy = m_SceneCopy && m_CopyPipeline && display && l_Main.Width == l_Width && l_Main.Height == l_Height;

        FrameGraph& l_Graph = *m_FrameGraph;
        const FrameGraphTexture l_Output = l_Graph.ImportTexture("Output", output, GetOutputDescription(l_Width, l_Height, GetOutputFormat()), RHI::ResourceState::Undefined, m_SwapChain ? RHI::ResourceState::Present : RHI::ResourceState::RenderTarget);
        l_Graph.AddPass("Output", FrameGraphPassType::Raster, [this, l_Output, l_Copy](FrameGraphPassBuilder& builder)
        {
            builder.AddColorAttachment({ l_Output, l_Copy ? RHI::LoadOp::DontCare : RHI::LoadOp::Clear, m_ClearColor });
            for (const FrameGraphTexture it_Display : m_Displays)
            {
                builder.Read(it_Display, RHI::ResourceState::ShaderResource);
            }
        }, [this, display, l_Copy, &layers](const FrameGraphContext& context)
        {
            RHI::CommandList& l_Commands = context.GetCommands();
            if (l_Copy)
            {
                const std::array<std::uint32_t, 2> l_PushData{ context.GetDevice().GetShaderResourceIndex(context.GetTexture(display)), 0 };

                l_Commands.SetPipeline(m_CopyPipeline);
                l_Commands.PushConstants(std::as_bytes(std::span(l_PushData)));
                l_Commands.Draw(3, 1, 0, 0);
            }

            TR_PROFILE_SCOPE("LayerStack::OnRenderUI");
            for (const Scope<Layer>& it_Layer : layers)
            {
                it_Layer->OnRenderUI(l_Commands);
            }
        });
    }

    // Each added output is cleared, then drawn by its callback, which may show any view's display target. Minimized and zero-sized windows are skipped, and a swap chain left unacquired ignores Present
    void Renderer::AddAddedOutputPasses()
    {
        FrameGraph& l_Graph = *m_FrameGraph;
        for (Output& it_Output : m_Outputs)
        {
            if (it_Output.Target->GetWidth() == 0 || it_Output.Target->GetHeight() == 0 || it_Output.Target->IsMinimized())
            {
                continue;
            }

            const RHI::TextureHandle l_Texture = it_Output.SwapChain ? it_Output.SwapChain->AcquireNextTexture() : it_Output.Offscreen;
            if (!l_Texture)
            {
                continue;
            }

            const std::uint32_t l_Width = it_Output.SwapChain ? it_Output.SwapChain->GetWidth() : it_Output.Width;
            const std::uint32_t l_Height = it_Output.SwapChain ? it_Output.SwapChain->GetHeight() : it_Output.Height;

            const FrameGraphTexture l_Output = l_Graph.ImportTexture("Added output", l_Texture, GetOutputDescription(l_Width, l_Height, GetOutputFormat()), RHI::ResourceState::Undefined, it_Output.SwapChain ? RHI::ResourceState::Present : RHI::ResourceState::RenderTarget);
            const Output* l_Added = &it_Output;
            l_Graph.AddPass("Added output", FrameGraphPassType::Raster, [this, l_Output, l_Added](FrameGraphPassBuilder& builder)
            {
                builder.AddColorAttachment({ l_Output, RHI::LoadOp::Clear, l_Added->ClearColor });
                for (const FrameGraphTexture it_Display : m_Displays)
                {
                    builder.Read(it_Display, RHI::ResourceState::ShaderResource);
                }
            }, [l_Added](const FrameGraphContext& context)
            {
                if (l_Added->Callback)
                {
                    l_Added->Callback(context.GetCommands(), context.GetWidth(), context.GetHeight());
                }
            });
        }
    }

    // A window with no native handle draws into an offscreen target, as the main window does when headless. Extra windows present without vsync, so only the main window paces the frame
    std::uint32_t Renderer::AddOutput(Window& window, const std::array<float, 4>& clearColor, OutputCallback callback)
    {
        Output l_Output;
        l_Output.Id = m_NextOutputId;
        l_Output.Target = &window;
        l_Output.Width = std::max(window.GetWidth(), 1u);
        l_Output.Height = std::max(window.GetHeight(), 1u);
        l_Output.ClearColor = clearColor;
        l_Output.Callback = std::move(callback);

        if (window.GetNativeHandle() != nullptr)
        {
            RHI::SwapChainSpecification l_Specification;
            l_Specification.NativeWindow = window.GetNativeHandle();
            l_Specification.Width = l_Output.Width;
            l_Specification.Height = l_Output.Height;
            l_Specification.VSync = false;

            l_Output.SwapChain = m_Device.CreateSwapChain(l_Specification);
            if (!l_Output.SwapChain)
            {
                TR_CORE_ERROR("Renderer: {} cannot present to an added window", ToString(m_Device.GetInfo().API));

                return 0;
            }

            // Callbacks draw with pipelines built for the main window's format
            if (l_Output.SwapChain->GetFormat() != GetOutputFormat())
            {
                TR_CORE_ERROR("Renderer: an added window presents {}, but the main window presents {}", RHI::ToString(l_Output.SwapChain->GetFormat()), RHI::ToString(GetOutputFormat()));

                return 0;
            }
        }
        else
        {
            l_Output.Offscreen = CreateOutputTarget(l_Output.Width, l_Output.Height, "Renderer added output");
        }

        ++m_NextOutputId;
        m_Outputs.push_back(std::move(l_Output));

        return m_Outputs.back().Id;
    }

    // Between frames only, since a swap chain waits for the GPU as it goes
    void Renderer::RemoveOutput(std::uint32_t output)
    {
        const auto l_Output = std::ranges::find(m_Outputs, output, &Output::Id);
        if (l_Output == m_Outputs.end())
        {
            return;
        }

        DestroyOutput(*l_Output);
        m_Outputs.erase(l_Output);
    }

    void Renderer::DestroyOutput(Output& output)
    {
        output.SwapChain.reset();
        m_Device.DestroyTexture(output.Offscreen);
        output.Offscreen = {};
    }

    ViewID Renderer::CreateView(std::uint32_t width, std::uint32_t height)
    {
        return AddView(m_NextViewId++, width, height);
    }

    void Renderer::DestroyView(ViewID view)
    {
        if (view == c_MainView)
        {
            TR_CORE_ERROR("Renderer: the main view cannot be destroyed");

            return;
        }

        const auto a_View = std::ranges::find(m_Views, view, [](const Scope<View>& entry) { return entry->Id; });
        TR_CORE_ASSERT(a_View != m_Views.end(), "Renderer view {} is destroyed but does not exist.", view);
        if (a_View == m_Views.end())
        {
            return;
        }

        DestroyViewResources(**a_View);
        m_Views.erase(a_View);
    }

    bool Renderer::HasView(ViewID view) const
    {
        return std::ranges::any_of(m_Views, [view](const Scope<View>& entry) { return entry->Id == view; });
    }

    // A view's targets, and a pick readback for each frame in flight, which lasts as long as the view does
    ViewID Renderer::AddView(ViewID id, std::uint32_t width, std::uint32_t height)
    {
        Scope<View> l_View = CreateScope<View>();
        l_View->Id = id;
        l_View->RequestedWidth = width;
        l_View->RequestedHeight = height;
        CreateViewTargets(*l_View);

        RHI::BufferDescription l_Readback;
        l_Readback.Size = c_PickSize;
        l_Readback.Usage = RHI::BufferUsage::CopyDestination;
        l_Readback.Memory = RHI::MemoryType::Readback;
        l_Readback.DebugName = "Picked entity readback";
        for (RHI::BufferHandle& it_Readback : l_View->PickReadbacks)
        {
            it_Readback = m_Device.CreateBuffer(l_Readback);
        }

        m_Views.push_back(std::move(l_View));

        return id;
    }

    // Through the release queue, so a frame in flight never loses what it draws with
    void Renderer::DestroyViewResources(View& view)
    {
        view.Draws.Clear();
        m_Device.DestroyTexture(view.SceneTarget);
        m_Device.DestroyTexture(view.DisplayTarget);
        view.SceneTarget = {};
        view.DisplayTarget = {};
        for (RHI::BufferHandle& it_Readback : view.PickReadbacks)
        {
            m_Device.DestroyBuffer(it_Readback);
            it_Readback = {};
        }
    }

    Renderer::View* Renderer::FindView(ViewID view) const
    {
        const auto a_View = std::ranges::find(m_Views, view, [](const Scope<View>& entry) { return entry->Id; });
        TR_CORE_ASSERT(a_View != m_Views.end(), "Renderer view {} does not exist.", view);

        return a_View != m_Views.end() ? a_View->get() : nullptr;
    }

    const SceneDrawList& Renderer::GetSceneDraws(ViewID view) const
    {
        const View* l_View = FindView(view);

        return (l_View != nullptr ? *l_View : *m_Views.front()).Draws;
    }

    void Renderer::SetToneMapping(const ToneMapping& toneMapping, ViewID view)
    {
        if (View* l_View = FindView(view))
        {
            l_View->Mapping = toneMapping;
        }
    }

    const ToneMapping& Renderer::GetToneMapping(ViewID view) const
    {
        const View* l_View = FindView(view);

        return (l_View != nullptr ? *l_View : *m_Views.front()).Mapping;
    }

    RHI::TextureHandle Renderer::GetSceneTarget(ViewID view) const
    {
        const View* l_View = FindView(view);

        return l_View != nullptr ? l_View->SceneTarget : RHI::TextureHandle{};
    }

    RHI::TextureHandle Renderer::GetDisplayTarget(ViewID view) const
    {
        const View* l_View = FindView(view);

        return l_View != nullptr ? l_View->DisplayTarget : RHI::TextureHandle{};
    }

    std::uint32_t Renderer::GetSceneWidth(ViewID view) const
    {
        const View* l_View = FindView(view);

        return l_View != nullptr ? l_View->Width : 0;
    }

    std::uint32_t Renderer::GetSceneHeight(ViewID view) const
    {
        const View* l_View = FindView(view);

        return l_View != nullptr ? l_View->Height : 0;
    }

    RHI::Format Renderer::GetSceneFormat() const
    {
        return c_SceneFormat;
    }

    RHI::Format Renderer::GetDisplayFormat() const
    {
        return c_DisplayFormat;
    }

    RHI::Format Renderer::GetOutputFormat() const
    {
        return m_SwapChain ? m_SwapChain->GetFormat() : RHI::Format::BGRA8Unorm;
    }

    // Taken between frames. Width and height of 0 make the view follow the output again, as the main view does until this is called
    void Renderer::SetSceneSize(std::uint32_t width, std::uint32_t height, ViewID view)
    {
        if (View* l_View = FindView(view))
        {
            l_View->RequestedWidth = width;
            l_View->RequestedHeight = height;
        }
    }

    std::uint32_t Renderer::GetWantedWidth(const View& view) const
    {
        return view.RequestedWidth > 0 && view.RequestedHeight > 0 ? view.RequestedWidth : GetOutputWidth();
    }

    std::uint32_t Renderer::GetWantedHeight(const View& view) const
    {
        return view.RequestedWidth > 0 && view.RequestedHeight > 0 ? view.RequestedHeight : GetOutputHeight();
    }

    std::uint32_t Renderer::GetOutputWidth() const
    {
        return m_SwapChain ? m_SwapChain->GetWidth() : std::max(m_TargetWidth, 1u);
    }

    std::uint32_t Renderer::GetOutputHeight() const
    {
        return m_SwapChain ? m_SwapChain->GetHeight() : std::max(m_TargetHeight, 1u);
    }

    void Renderer::SetVSync(bool enabled)
    {
        s_VSyncVariable.Set(enabled);
    }

    bool Renderer::IsVSync() const
    {
        return s_VSyncVariable.Get();
    }

    // Applies a new window size or vsync setting between frames, where nothing in flight uses the old swap chain images
    void Renderer::FollowWindow()
    {
        const bool l_VSync = s_VSyncVariable.Get();
        if (l_VSync != m_VSync)
        {
            m_VSync = l_VSync;
            if (m_SwapChain)
            {
                m_SwapChain->SetVSync(m_VSync);
            }

            TR_CORE_INFO("Renderer: vsync {}", m_VSync ? "on" : "off");
        }

        if (m_Window.GetWidth() != m_TargetWidth || m_Window.GetHeight() != m_TargetHeight)
        {
            m_TargetWidth = m_Window.GetWidth();
            m_TargetHeight = m_Window.GetHeight();

            if (m_SwapChain)
            {
                m_SwapChain->Resize(m_TargetWidth, m_TargetHeight);
            }
            else
            {
                m_Device.DestroyTexture(m_OffscreenTarget);
                CreateOffscreenTarget();
            }
        }

        FollowOutputs();

        // Also catches a swap chain that changed size on its own while acquiring, for every view that follows it. The old targets are released only once no frame in flight can still show them
        for (const Scope<View>& it_View : m_Views)
        {
            View& l_View = *it_View;
            if (l_View.Width != GetWantedWidth(l_View) || l_View.Height != GetWantedHeight(l_View))
            {
                m_Device.DestroyTexture(l_View.SceneTarget);
                m_Device.DestroyTexture(l_View.DisplayTarget);
                CreateViewTargets(l_View);
            }
        }
    }

    // Added outputs take a new size between frames, as the main window does. A minimized window keeps its old size until it comes back
    void Renderer::FollowOutputs()
    {
        for (Output& it_Output : m_Outputs)
        {
            const std::uint32_t l_Width = it_Output.Target->GetWidth();
            const std::uint32_t l_Height = it_Output.Target->GetHeight();
            if (l_Width == 0 || l_Height == 0 || (l_Width == it_Output.Width && l_Height == it_Output.Height))
            {
                continue;
            }

            it_Output.Width = l_Width;
            it_Output.Height = l_Height;

            if (it_Output.SwapChain)
            {
                it_Output.SwapChain->Resize(l_Width, l_Height);
            }
            else
            {
                m_Device.DestroyTexture(it_Output.Offscreen);
                it_Output.Offscreen = CreateOutputTarget(l_Width, l_Height, "Renderer added output");
            }
        }
    }

    void Renderer::CreateOffscreenTarget()
    {
        m_OffscreenTarget = CreateOutputTarget(std::max(m_TargetWidth, 1u), std::max(m_TargetHeight, 1u), "Renderer offscreen target");
    }

    RHI::TextureHandle Renderer::CreateOutputTarget(std::uint32_t width, std::uint32_t height, std::string_view debugName)
    {
        RHI::TextureDescription l_Description;
        l_Description.Width = width;
        l_Description.Height = height;
        l_Description.TextureFormat = RHI::Format::BGRA8Unorm;
        l_Description.Usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::CopySource;
        l_Description.OptimizedClear = false;
        l_Description.DebugName = debugName;

        return m_Device.CreateTexture(l_Description);
    }

    // Layers can change the clear colour every frame, so a scene target asks for no optimized clear value. The display target is the scene's size, and the tonemap pass writes all of it
    void Renderer::CreateViewTargets(View& view)
    {
        RHI::TextureDescription l_Description;
        l_Description.Width = GetWantedWidth(view);
        l_Description.Height = GetWantedHeight(view);
        l_Description.TextureFormat = c_SceneFormat;
        l_Description.Usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::ShaderResource | RHI::TextureUsage::CopySource;
        l_Description.OptimizedClear = false;
        l_Description.DebugName = view.Id == c_MainView ? "Renderer scene target" : "Renderer view scene target";

        view.SceneTarget = m_Device.CreateTexture(l_Description);
        view.SceneDescription = l_Description;
        view.SceneState = RHI::ResourceState::Undefined;
        view.Width = l_Description.Width;
        view.Height = l_Description.Height;

        l_Description.TextureFormat = c_DisplayFormat;
        l_Description.DebugName = view.Id == c_MainView ? "Renderer display target" : "Renderer view display target";
        view.DisplayTarget = m_Device.CreateTexture(l_Description);
        view.DisplayDescription = l_Description;
        view.DisplayState = RHI::ResourceState::Undefined;
    }

    // One triangle over the whole target from a shader's VertexMain and PixelMain, as the scene copy and the tonemap are drawn
    RHI::PipelineHandle Renderer::CreateFullscreenPipeline(std::string_view shader, RHI::Format format, std::string_view debugName, std::string_view consequence, bool alphaBlend)
    {
        const std::string_view l_Extension = m_Device.GetInfo().API == GraphicsAPI::D3D12 ? "dxil" : "spv";
        const Expected<FileBuffer, FileError> l_VertexShader = FileSystem::ReadFile(std::format("/engine/shaders/{}.VertexMain.{}", shader, l_Extension));
        const Expected<FileBuffer, FileError> l_PixelShader = FileSystem::ReadFile(std::format("/engine/shaders/{}.PixelMain.{}", shader, l_Extension));
        if (!l_VertexShader || !l_PixelShader)
        {
            TR_CORE_INFO("Renderer: no {} {} shaders under /engine/shaders, so {}", l_Extension, shader, consequence);

            return {};
        }

        const std::array<RHI::Format, 1> l_ColorFormats{ format };

        RHI::GraphicsPipelineDescription l_Description;
        l_Description.VertexShader = { *l_VertexShader, "VertexMain" };
        l_Description.PixelShader = { *l_PixelShader, "PixelMain" };
        l_Description.ColorFormats = l_ColorFormats;
        l_Description.Cull = RHI::CullMode::None;
        l_Description.AlphaBlend = alphaBlend;
        l_Description.DebugName = debugName;

        const RHI::PipelineHandle l_Pipeline = m_Device.CreateGraphicsPipeline(l_Description);
        if (!l_Pipeline)
        {
            TR_CORE_ERROR("Renderer: the {} pipeline could not be created, so {}", debugName, consequence);
        }

        return l_Pipeline;
    }

    // The pick's compute pipeline and the outline's, which every view shares
    void Renderer::CreatePicking()
    {
        const std::string_view l_Extension = m_Device.GetInfo().API == GraphicsAPI::D3D12 ? "dxil" : "spv";
        const Expected<FileBuffer, FileError> l_Pick = FileSystem::ReadFile(std::format("/engine/shaders/EntityPick.PickEntity.{}", l_Extension));
        if (l_Pick)
        {
            RHI::ComputePipelineDescription l_Description;
            l_Description.ComputeShader = { *l_Pick, "PickEntity" };
            l_Description.DebugName = "Entity pick";
            m_PickPipeline = m_Device.CreateComputePipeline(l_Description);
        }

        if (!m_PickPipeline)
        {
            TR_CORE_INFO("Renderer: no {} EntityPick shader under /engine/shaders, so nothing can be picked", l_Extension);
        }

        m_OutlinePipeline = CreateFullscreenPipeline("SelectionOutline", c_DisplayFormat, "Selection outline", "the selection is not outlined", true);
    }

    // The frame rate is added again at the next report
    void Renderer::SetTitle(std::string_view title)
    {
        m_Title = title;
        m_Window.SetTitle(std::format("{} - {}", m_Title, ToString(m_Device.GetInfo().API)));
    }

    // Once a second, in the window title
    void Renderer::ReportFrameRate()
    {
        const auto l_Now = std::chrono::steady_clock::now();
        const double l_Seconds = std::chrono::duration<double>(l_Now - m_ReportTime).count();
        if (l_Seconds < 1.0)
        {
            return;
        }

        m_Window.SetTitle(std::format("{} - {} - {:.0f} fps", m_Title, ToString(m_Device.GetInfo().API), static_cast<double>(m_FramesSinceReport) / l_Seconds));

        m_ReportTime = l_Now;
        m_FramesSinceReport = 0;
    }
}