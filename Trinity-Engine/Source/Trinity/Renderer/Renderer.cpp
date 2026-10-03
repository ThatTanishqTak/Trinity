#include "Trinity/Renderer/Renderer.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Core/Window.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"

#include <algorithm>
#include <format>

namespace Trinity
{
    Renderer::Renderer(RHI::Device& device, Window& window, std::string_view title) : m_Device(device), m_Window(window), m_Title(title)
    {
        const RHI::DeviceInfo& l_Info = m_Device.GetInfo();

        if (m_Window.GetNativeHandle() != nullptr)
        {
            RHI::SwapChainSpecification l_Specification;
            l_Specification.NativeWindow = m_Window.GetNativeHandle();
            l_Specification.Width = m_Window.GetWidth();
            l_Specification.Height = m_Window.GetHeight();

            m_SwapChain = m_Device.CreateSwapChain(l_Specification);
            if (!m_SwapChain)
            {
                TR_CORE_ERROR("Renderer: {} cannot present to the window, so frames go to an offscreen target instead", ToString(l_Info.API));
            }
        }

        if (!m_SwapChain)
        {
            RHI::TextureDescription l_Description;
            l_Description.Width = std::max(m_Window.GetWidth(), 1u);
            l_Description.Height = std::max(m_Window.GetHeight(), 1u);
            l_Description.TextureFormat = RHI::Format::BGRA8Unorm;
            l_Description.Usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::CopySource;
            l_Description.ClearColor = m_ClearColor;
            l_Description.DebugName = "Renderer offscreen target";

            m_OffscreenTarget = m_Device.CreateTexture(l_Description);
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

    void Renderer::RenderFrame()
    {
        TR_PROFILE_FUNCTION();

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