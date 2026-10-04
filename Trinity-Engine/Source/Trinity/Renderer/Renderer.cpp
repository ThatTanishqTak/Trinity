#include "Trinity/Renderer/Renderer.hpp"

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

namespace Trinity
{
    namespace
    {
        ConsoleVariable<bool> s_VSyncVariable("renderer.vsync", true, "Waits for the display's vertical blank before showing each frame");

        constexpr RHI::Format c_SceneFormat = RHI::Format::BGRA8Unorm;

        void SetFullViewport(RHI::CommandList& commands, std::uint32_t width, std::uint32_t height)
        {
            commands.SetViewport({ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f });
            commands.SetScissor({ 0, 0, width, height });
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
        CreateCopyPipeline();

        m_StartTime = std::chrono::steady_clock::now();
        m_ReportTime = m_StartTime;

        TR_CORE_INFO("Renderer: {} on {}, drawing to {} through a {} scene target", ToString(l_Info.API), l_Info.AdapterName, m_SwapChain ? std::format("the window ({}x{})", m_SwapChain->GetWidth(), m_SwapChain->GetHeight()) : std::format("an offscreen {}x{} target", m_Window.GetWidth(), m_Window.GetHeight()), RHI::ToString(c_SceneFormat));
    }

    Renderer::~Renderer()
    {
        const double l_Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_StartTime).count();
        TR_CORE_INFO("Renderer: {} frame(s) on {} at an average of {:.1f} fps", m_FrameCount, ToString(m_Device.GetInfo().API), l_Seconds > 0.0 ? static_cast<double>(m_FrameCount) / l_Seconds : 0.0);

        m_SwapChain.reset();
        m_Device.DestroyPipeline(m_CopyPipeline);
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

        {
            TR_PROFILE_SCOPE("LayerStack::OnPrepareRender");
            for (const Scope<Layer>& it_Layer : layers)
            {
                it_Layer->OnPrepareRender(l_Commands);
            }
        }

        RenderScene(l_Commands, layers);

        const RHI::TextureHandle l_Output = m_SwapChain ? m_SwapChain->AcquireNextTexture() : m_OffscreenTarget;
        if (l_Output)
        {
            RenderOutput(l_Commands, l_Output, layers);
        }

        m_Device.EndFrame();

        if (m_SwapChain)
        {
            m_SwapChain->Present();
        }

        ++m_FrameCount;
        ++m_FramesSinceReport;
        ReportFrameRate();
    }

    // Layers draw into the scene target, which then waits as a shader resource for the output pass
    void Renderer::RenderScene(RHI::CommandList& commands, LayerStack& layers)
    {
        if (!m_SceneTarget)
        {
            return;
        }

        commands.TextureBarrier(m_SceneTarget, m_SceneState, RHI::ResourceState::RenderTarget);

        const std::array<RHI::ColorAttachment, 1> l_Attachments{ RHI::ColorAttachment{ m_SceneTarget, RHI::LoadOp::Clear, RHI::StoreOp::Store, m_ClearColor } };
        RHI::RenderingDescription l_Rendering;
        l_Rendering.ColorAttachments = l_Attachments;
        commands.BeginRendering(l_Rendering);
        SetFullViewport(commands, m_SceneWidth, m_SceneHeight);

        {
            TR_PROFILE_SCOPE("LayerStack::OnRender");
            for (const Scope<Layer>& it_Layer : layers)
            {
                it_Layer->OnRender(commands);
            }
        }

        commands.EndRendering();
        commands.TextureBarrier(m_SceneTarget, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);
        m_SceneState = RHI::ResourceState::ShaderResource;
    }

    // The copy reads one scene texel per output pixel, so the scene reaches the output exactly as drawn, and the UI goes over it
    void Renderer::RenderOutput(RHI::CommandList& commands, RHI::TextureHandle output, LayerStack& layers)
    {
        const std::uint32_t l_Width = GetOutputWidth();
        const std::uint32_t l_Height = GetOutputHeight();

        // A swap chain recreated while acquiring can differ from the scene target for one frame, which then shows the clear colour
        const bool l_Copy = m_SceneCopy && m_CopyPipeline && m_SceneTarget && m_SceneWidth == l_Width && m_SceneHeight == l_Height;

        commands.TextureBarrier(output, RHI::ResourceState::Undefined, RHI::ResourceState::RenderTarget);

        const std::array<RHI::ColorAttachment, 1> l_Attachments{ RHI::ColorAttachment{ output, l_Copy ? RHI::LoadOp::DontCare : RHI::LoadOp::Clear, RHI::StoreOp::Store, m_ClearColor } };
        RHI::RenderingDescription l_Rendering;
        l_Rendering.ColorAttachments = l_Attachments;
        commands.BeginRendering(l_Rendering);
        SetFullViewport(commands, l_Width, l_Height);

        if (l_Copy)
        {
            const std::array<std::uint32_t, 2> l_PushData{ m_Device.GetShaderResourceIndex(m_SceneTarget), 0 };

            commands.SetPipeline(m_CopyPipeline);
            commands.PushConstants(std::as_bytes(std::span(l_PushData)));
            commands.Draw(3, 1, 0, 0);
        }

        {
            TR_PROFILE_SCOPE("LayerStack::OnRenderUI");
            for (const Scope<Layer>& it_Layer : layers)
            {
                it_Layer->OnRenderUI(commands);
            }
        }

        commands.EndRendering();

        if (m_SwapChain)
        {
            commands.TextureBarrier(output, RHI::ResourceState::RenderTarget, RHI::ResourceState::Present);
        }
    }

    RHI::Format Renderer::GetSceneFormat() const
    {
        return c_SceneFormat;
    }

    RHI::Format Renderer::GetOutputFormat() const
    {
        return m_SwapChain ? m_SwapChain->GetFormat() : RHI::Format::BGRA8Unorm;
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

        // Also catches a swap chain that changed size on its own while acquiring
        if (m_SceneWidth != GetOutputWidth() || m_SceneHeight != GetOutputHeight())
        {
            m_Device.DestroyTexture(m_SceneTarget);
            CreateSceneTarget();
        }
    }

    void Renderer::CreateOffscreenTarget()
    {
        RHI::TextureDescription l_Description;
        l_Description.Width = std::max(m_TargetWidth, 1u);
        l_Description.Height = std::max(m_TargetHeight, 1u);
        l_Description.TextureFormat = RHI::Format::BGRA8Unorm;
        l_Description.Usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::CopySource;
        l_Description.OptimizedClear = false;
        l_Description.DebugName = "Renderer offscreen target";

        m_OffscreenTarget = m_Device.CreateTexture(l_Description);
    }

    // Layers can change the clear colour every frame, so the scene target asks for no optimized clear value
    void Renderer::CreateSceneTarget()
    {
        RHI::TextureDescription l_Description;
        l_Description.Width = GetOutputWidth();
        l_Description.Height = GetOutputHeight();
        l_Description.TextureFormat = c_SceneFormat;
        l_Description.Usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::ShaderResource | RHI::TextureUsage::CopySource;
        l_Description.OptimizedClear = false;
        l_Description.DebugName = "Renderer scene target";

        m_SceneTarget = m_Device.CreateTexture(l_Description);
        m_SceneState = RHI::ResourceState::Undefined;
        m_SceneWidth = l_Description.Width;
        m_SceneHeight = l_Description.Height;
    }

    void Renderer::CreateCopyPipeline()
    {
        const std::string_view l_Extension = m_Device.GetInfo().API == GraphicsAPI::D3D12 ? "dxil" : "spv";
        const Expected<FileBuffer, FileError> l_VertexShader = FileSystem::ReadFile(std::format("/engine/shaders/SceneCopy.VertexMain.{}", l_Extension));
        const Expected<FileBuffer, FileError> l_PixelShader = FileSystem::ReadFile(std::format("/engine/shaders/SceneCopy.PixelMain.{}", l_Extension));
        if (!l_VertexShader || !l_PixelShader)
        {
            TR_CORE_INFO("Renderer: no {} scene copy shaders under /engine/shaders, so the output only shows the clear colour", l_Extension);

            return;
        }

        const std::array<RHI::Format, 1> l_ColorFormats{ GetOutputFormat() };

        RHI::GraphicsPipelineDescription l_Description;
        l_Description.VertexShader = { *l_VertexShader, "VertexMain" };
        l_Description.PixelShader = { *l_PixelShader, "PixelMain" };
        l_Description.ColorFormats = l_ColorFormats;
        l_Description.Cull = RHI::CullMode::None;
        l_Description.DebugName = "Renderer scene copy";

        m_CopyPipeline = m_Device.CreateGraphicsPipeline(l_Description);
        if (!m_CopyPipeline)
        {
            TR_CORE_ERROR("Renderer: the scene copy pipeline could not be created, so the output only shows the clear colour");
        }
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