#include "Trinity/Renderer/Renderer.hpp"

#include "Trinity/Asset/AssetManager.hpp"
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
#include <format>
#include <span>
#include <utility>

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

        CreateSceneTarget();
        m_CopyPipeline = CreateFullscreenPipeline("SceneCopy", GetOutputFormat(), "Renderer scene copy", "the output only shows the clear colour");
        m_TonemapPipeline = CreateFullscreenPipeline("Tonemap", c_DisplayFormat, "Renderer tonemap", "the scene is shown as black");

        // Textures, materials and meshes need this device, so their loaders live exactly as long as the renderer. Materials keep textures loaded, so they go before textures do
        m_TextureLoader = CreateScope<TextureLoader>(m_Device);
        AssetManager::RegisterLoader(*m_TextureLoader);
        m_MaterialLoader = CreateScope<MaterialLoader>(m_Device);
        AssetManager::RegisterLoader(*m_MaterialLoader);
        m_MeshLoader = CreateScope<MeshLoader>(m_Device);
        AssetManager::RegisterLoader(*m_MeshLoader);

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
        m_SceneDraws.Clear();
        m_Renderer3D.reset();
        m_Renderer2D.reset();
        AssetManager::UnregisterLoader(MeshAsset::c_AssetType);
        m_MeshLoader.reset();
        AssetManager::UnregisterLoader(MaterialAsset::c_AssetType);
        m_MaterialLoader.reset();
        AssetManager::UnregisterLoader(TextureAsset::c_AssetType);
        m_TextureLoader.reset();

        m_Outputs.clear();
        m_SwapChain.reset();
        m_Device.DestroyPipeline(m_TonemapPipeline);
        m_Device.DestroyPipeline(m_CopyPipeline);
        m_Device.DestroyTexture(m_DisplayTarget);
        m_Device.DestroyTexture(m_SceneTarget);
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
        m_TextureLoader->RecordUploads(l_Commands);
        m_MaterialLoader->RecordUploads(l_Commands);
        m_MeshLoader->RecordUploads(l_Commands);
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
        m_SceneSubmitted = false;
        m_SceneState = m_SceneTarget ? RHI::ResourceState::ShaderResource : RHI::ResourceState::Undefined;
        m_DisplayState = m_SceneTarget && m_DisplayTarget ? RHI::ResourceState::ShaderResource : m_DisplayState;

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

    // The scene and display targets are imported in whatever state the last frame left them, and both end as shader resources. The scene is cleared to the linear clear colour, by the opaque pass when a scene was submitted, and every pass that can show the display target, the UI's included, reads it
    void Renderer::BuildFrameGraph(LayerStack& layers)
    {
        TR_PROFILE_FUNCTION();

        FrameGraph& l_Graph = *m_FrameGraph;
        l_Graph.Reset();

        FrameGraphTexture l_Scene;
        if (m_SceneTarget)
        {
            const std::array<float, 4> l_Clear{ SrgbToLinear(m_ClearColor[0]), SrgbToLinear(m_ClearColor[1]), SrgbToLinear(m_ClearColor[2]), m_ClearColor[3] };
            l_Scene = l_Graph.ImportTexture("Scene", m_SceneTarget, m_SceneDescription, m_SceneState, RHI::ResourceState::ShaderResource);
            if (m_SceneSubmitted)
            {
                static_cast<void>(m_Renderer3D->AddPasses(l_Graph, m_SceneDraws, l_Scene, m_SceneDescription, l_Clear, m_SceneOptions));
            }

            const RHI::LoadOp l_Load = m_SceneSubmitted ? RHI::LoadOp::Load : RHI::LoadOp::Clear;
            l_Graph.AddPass("Scene", FrameGraphPassType::Raster, [l_Scene, l_Clear, l_Load](FrameGraphPassBuilder& builder) { builder.AddColorAttachment({ l_Scene, l_Load, l_Clear }); }, [&layers](const FrameGraphContext& context)
            {
                TR_PROFILE_SCOPE("LayerStack::OnRender");
                for (const Scope<Layer>& it_Layer : layers)
                {
                    it_Layer->OnRender(context.GetCommands());
                }
            });
        }

        {
            TR_PROFILE_SCOPE("LayerStack::OnBuildFrameGraph");
            for (const Scope<Layer>& it_Layer : layers)
            {
                it_Layer->OnBuildFrameGraph(l_Graph, l_Scene);
            }
        }

        FrameGraphTexture l_Display;
        if (l_Scene && m_DisplayTarget)
        {
            l_Display = l_Graph.ImportTexture("Display", m_DisplayTarget, m_DisplayDescription, m_DisplayState, RHI::ResourceState::ShaderResource);
            AddTonemapPass(l_Graph, l_Scene, l_Display, m_ToneMapping);
        }

        const RHI::TextureHandle l_Output = m_SwapChain ? m_SwapChain->AcquireNextTexture() : m_OffscreenTarget;
        if (l_Output)
        {
            AddOutputPass(l_Display, l_Output, layers);
        }

        AddAddedOutputPasses(l_Display);
    }

    void Renderer::SubmitScene(Scene& scene, const RenderView& view, const SceneOptions& options)
    {
        m_Renderer3D->Collect(scene, view, m_SceneDraws);
        m_SceneOptions = options;
        m_SceneSubmitted = true;
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

    // The copy reads one display texel per output pixel, so the tone mapped scene reaches the output exactly as written, and the UI goes over it
    void Renderer::AddOutputPass(FrameGraphTexture display, RHI::TextureHandle output, LayerStack& layers)
    {
        const std::uint32_t l_Width = GetOutputWidth();
        const std::uint32_t l_Height = GetOutputHeight();

        // A swap chain recreated while acquiring can differ from the display target for one frame, which then shows the clear colour
        const bool l_Copy = m_SceneCopy && m_CopyPipeline && display && m_SceneWidth == l_Width && m_SceneHeight == l_Height;

        FrameGraph& l_Graph = *m_FrameGraph;
        const FrameGraphTexture l_Output = l_Graph.ImportTexture("Output", output, GetOutputDescription(l_Width, l_Height, GetOutputFormat()), RHI::ResourceState::Undefined, m_SwapChain ? RHI::ResourceState::Present : RHI::ResourceState::RenderTarget);
        l_Graph.AddPass("Output", FrameGraphPassType::Raster, [this, display, l_Output, l_Copy](FrameGraphPassBuilder& builder)
        {
            builder.AddColorAttachment({ l_Output, l_Copy ? RHI::LoadOp::DontCare : RHI::LoadOp::Clear, m_ClearColor });
            if (display)
            {
                builder.Read(display, RHI::ResourceState::ShaderResource);
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

    // Each added output is cleared, then drawn by its callback, which may show the display target. Minimized and zero-sized windows are skipped, and a swap chain left unacquired ignores Present
    void Renderer::AddAddedOutputPasses(FrameGraphTexture display)
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
            l_Graph.AddPass("Added output", FrameGraphPassType::Raster, [display, l_Output, l_Added](FrameGraphPassBuilder& builder)
            {
                builder.AddColorAttachment({ l_Output, RHI::LoadOp::Clear, l_Added->ClearColor });
                if (display)
                {
                    builder.Read(display, RHI::ResourceState::ShaderResource);
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

    // Taken between frames. Width and height of 0 make the scene follow the output again, as it does until this is called
    void Renderer::SetSceneSize(std::uint32_t width, std::uint32_t height)
    {
        m_RequestedSceneWidth = width;
        m_RequestedSceneHeight = height;
    }

    std::uint32_t Renderer::GetWantedSceneWidth() const
    {
        return m_RequestedSceneWidth > 0 && m_RequestedSceneHeight > 0 ? m_RequestedSceneWidth : GetOutputWidth();
    }

    std::uint32_t Renderer::GetWantedSceneHeight() const
    {
        return m_RequestedSceneWidth > 0 && m_RequestedSceneHeight > 0 ? m_RequestedSceneHeight : GetOutputHeight();
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

        // Also catches a swap chain that changed size on its own while acquiring. The old target is released only once no frame in flight can still show it
        if (m_SceneWidth != GetWantedSceneWidth() || m_SceneHeight != GetWantedSceneHeight())
        {
            m_Device.DestroyTexture(m_SceneTarget);
            m_Device.DestroyTexture(m_DisplayTarget);
            CreateSceneTarget();
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

    // Layers can change the clear colour every frame, so the scene target asks for no optimized clear value. The display target is the scene's size, and the tonemap pass writes all of it
    void Renderer::CreateSceneTarget()
    {
        RHI::TextureDescription l_Description;
        l_Description.Width = GetWantedSceneWidth();
        l_Description.Height = GetWantedSceneHeight();
        l_Description.TextureFormat = c_SceneFormat;
        l_Description.Usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::ShaderResource | RHI::TextureUsage::CopySource;
        l_Description.OptimizedClear = false;
        l_Description.DebugName = "Renderer scene target";

        m_SceneTarget = m_Device.CreateTexture(l_Description);
        m_SceneDescription = l_Description;
        m_SceneState = RHI::ResourceState::Undefined;
        m_SceneWidth = l_Description.Width;
        m_SceneHeight = l_Description.Height;

        l_Description.TextureFormat = c_DisplayFormat;
        l_Description.DebugName = "Renderer display target";
        m_DisplayTarget = m_Device.CreateTexture(l_Description);
        m_DisplayDescription = l_Description;
        m_DisplayState = RHI::ResourceState::Undefined;
    }

    // One triangle over the whole target from a shader's VertexMain and PixelMain, as the scene copy and the tonemap are drawn
    RHI::PipelineHandle Renderer::CreateFullscreenPipeline(std::string_view shader, RHI::Format format, std::string_view debugName, std::string_view consequence)
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
        l_Description.DebugName = debugName;

        const RHI::PipelineHandle l_Pipeline = m_Device.CreateGraphicsPipeline(l_Description);
        if (!l_Pipeline)
        {
            TR_CORE_ERROR("Renderer: the {} pipeline could not be created, so {}", debugName, consequence);
        }

        return l_Pipeline;
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