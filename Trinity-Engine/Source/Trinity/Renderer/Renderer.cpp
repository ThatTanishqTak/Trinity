#include "Trinity/Renderer/Renderer.hpp"

#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/LayerStack.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Core/Window.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"

#include <algorithm>
#include <format>

namespace Trinity
{
    namespace
    {
        ConsoleVariable<bool> s_VSyncVariable("renderer.vsync", true, "Waits for the display's vertical blank before showing each frame");
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

        m_StartTime = std::chrono::steady_clock::now();
        m_ReportTime = m_StartTime;

        TR_CORE_INFO("Renderer: {} on {}, drawing to {}", ToString(l_Info.API), l_Info.AdapterName, m_SwapChain ? std::format("the window ({}x{})", m_SwapChain->GetWidth(), m_SwapChain->GetHeight()) : std::format("an offscreen {}x{} target", m_Window.GetWidth(), m_Window.GetHeight()));
    }

    Renderer::~Renderer()
    {
        const double l_Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_StartTime).count();
        TR_CORE_INFO("Renderer: {} frame(s) on {} at an average of {:.1f} fps", m_FrameCount, ToString(m_Device.GetInfo().API), l_Seconds > 0.0 ? static_cast<double>(m_FrameCount) / l_Seconds : 0.0);

        m_SwapChain.reset();
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

        const RHI::TextureHandle l_Target = m_SwapChain ? m_SwapChain->AcquireNextTexture() : m_OffscreenTarget;
        if (l_Target)
        {
            // The clear covers the whole target, so whatever it held before is discarded
            l_Commands.TextureBarrier(l_Target, RHI::ResourceState::Undefined, RHI::ResourceState::RenderTarget);

            const std::array<RHI::ColorAttachment, 1> l_Attachments{ RHI::ColorAttachment{ l_Target, RHI::LoadOp::Clear, RHI::StoreOp::Store, m_ClearColor } };
            RHI::RenderingDescription l_Rendering;
            l_Rendering.ColorAttachments = l_Attachments;
            l_Commands.BeginRendering(l_Rendering);

            const std::uint32_t l_Width = m_SwapChain ? m_SwapChain->GetWidth() : std::max(m_TargetWidth, 1u);
            const std::uint32_t l_Height = m_SwapChain ? m_SwapChain->GetHeight() : std::max(m_TargetHeight, 1u);
            l_Commands.SetViewport({ 0.0f, 0.0f, static_cast<float>(l_Width), static_cast<float>(l_Height), 0.0f, 1.0f });
            l_Commands.SetScissor({ 0, 0, l_Width, l_Height });

            {
                TR_PROFILE_SCOPE("LayerStack::OnRender");
                for (const Scope<Layer>& it_Layer : layers)
                {
                    it_Layer->OnRender(l_Commands);
                }
            }

            l_Commands.EndRendering();

            if (m_SwapChain)
            {
                l_Commands.TextureBarrier(l_Target, RHI::ResourceState::RenderTarget, RHI::ResourceState::Present);
            }
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

    RHI::Format Renderer::GetTargetFormat() const
    {
        return m_SwapChain ? m_SwapChain->GetFormat() : RHI::Format::BGRA8Unorm;
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

        if (m_Window.GetWidth() == m_TargetWidth && m_Window.GetHeight() == m_TargetHeight)
        {
            return;
        }

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

    void Renderer::CreateOffscreenTarget()
    {
        RHI::TextureDescription l_Description;
        l_Description.Width = std::max(m_TargetWidth, 1u);
        l_Description.Height = std::max(m_TargetHeight, 1u);
        l_Description.TextureFormat = RHI::Format::BGRA8Unorm;
        l_Description.Usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::CopySource;
        l_Description.ClearColor = m_ClearColor;
        l_Description.DebugName = "Renderer offscreen target";

        m_OffscreenTarget = m_Device.CreateTexture(l_Description);
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