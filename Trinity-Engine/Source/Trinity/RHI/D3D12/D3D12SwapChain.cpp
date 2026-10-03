#include "Trinity/RHI/D3D12/D3D12SwapChain.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            using Microsoft::WRL::ComPtr;

            constexpr UINT c_BufferCount = 3;

            // Flip model buffers cannot be sRGB, so an sRGB swap chain gets plain buffers and sRGB views
            Format ToBufferFormat(Format format)
            {
                switch (format)
                {
                    case Format::BGRA8Srgb:
                    {
                        return Format::BGRA8Unorm;
                    }
                    case Format::RGBA8Srgb:
                    {
                        return Format::RGBA8Unorm;
                    }
                    case Format::BGRA8Unorm:
                    case Format::RGBA8Unorm:
                    case Format::RGBA16Float:
                    {
                        return format;
                    }
                    default:
                    {
                        return Format::BGRA8Unorm;
                    }
                }
            }
        }

        Scope<D3D12SwapChain> D3D12SwapChain::Create(D3D12Device& device, const SwapChainSpecification& specification)
        {
            Scope<D3D12SwapChain> l_SwapChain = CreateScope<D3D12SwapChain>(device, specification);
            if (!l_SwapChain->Initialize())
            {
                return nullptr;
            }

            return l_SwapChain;
        }

        D3D12SwapChain::D3D12SwapChain(D3D12Device& device, const SwapChainSpecification& specification) : m_Device(device), m_Specification(specification)
        {

        }

        D3D12SwapChain::~D3D12SwapChain()
        {
            m_Device.WaitIdle();
            RemoveBuffers();
            m_SwapChain.Reset();
        }

        bool D3D12SwapChain::Initialize()
        {
            const HWND l_Window = static_cast<HWND>(m_Specification.NativeWindow);
            const Format l_BufferFormat = ToBufferFormat(m_Specification.ImageFormat);
            const bool l_Srgb = m_Specification.ImageFormat == Format::BGRA8Srgb || m_Specification.ImageFormat == Format::RGBA8Srgb;
            m_Format = l_Srgb ? m_Specification.ImageFormat : l_BufferFormat;

            DXGI_SWAP_CHAIN_DESC1 l_Description{};
            l_Description.Width = m_Specification.Width;
            l_Description.Height = m_Specification.Height;
            l_Description.Format = ToDXGIFormat(l_BufferFormat);
            l_Description.SampleDesc.Count = 1;
            l_Description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            l_Description.BufferCount = c_BufferCount;
            l_Description.Scaling = DXGI_SCALING_STRETCH;
            l_Description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            l_Description.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
            l_Description.Flags = m_Device.IsTearingSupported() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

            ComPtr<IDXGISwapChain1> l_SwapChain;
            HRESULT l_Result = m_Device.GetFactory()->CreateSwapChainForHwnd(m_Device.GetQueue(), l_Window, &l_Description, nullptr, nullptr, &l_SwapChain);
            if (SUCCEEDED(l_Result))
            {
                l_Result = l_SwapChain.As(&m_SwapChain);
            }

            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: the swap chain could not be created ({})", FormatResult(l_Result));

                return false;
            }

            m_Device.GetFactory()->MakeWindowAssociation(l_Window, DXGI_MWA_NO_ALT_ENTER);

            if (!AddBuffers())
            {
                return false;
            }

            TR_CORE_INFO("D3D12: swap chain of {} {}x{} {} buffers, vsync {}{}", c_BufferCount, m_Width, m_Height, ToString(m_Format), m_Specification.VSync ? "on" : "off", m_Device.IsTearingSupported() ? ", tearing supported" : "");

            return true;
        }

        TextureHandle D3D12SwapChain::AcquireNextTexture()
        {
            TR_CORE_ASSERT(m_Device.IsInFrame(), "AcquireNextTexture is called between Device::BeginFrame and EndFrame.");

            const UINT l_Index = m_SwapChain->GetCurrentBackBufferIndex();
            m_Acquired = l_Index < m_Textures.size();

            return m_Acquired ? m_Textures[l_Index] : TextureHandle{};
        }

        void D3D12SwapChain::Present()
        {
            TR_CORE_ASSERT(!m_Device.IsInFrame(), "Present is called after Device::EndFrame.");

            if (!m_Acquired)
            {
                return;
            }

            m_Acquired = false;

            const bool l_Tearing = !m_Specification.VSync && m_Device.IsTearingSupported();
            const HRESULT l_Result = m_SwapChain->Present(m_Specification.VSync ? 1 : 0, l_Tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
            if (FAILED(l_Result) && !m_ReportedFailure)
            {
                TR_CORE_ERROR("D3D12: present failed ({})", FormatResult(l_Result));
                m_ReportedFailure = true;
            }
        }

        // The buffers are released first, since ResizeBuffers fails while anything still holds one
        void D3D12SwapChain::Resize(std::uint32_t width, std::uint32_t height)
        {
            if (width == 0 || height == 0)
            {
                return;
            }

            m_Device.WaitIdle();
            RemoveBuffers();

            const HRESULT l_Result = m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, m_Device.IsTearingSupported() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: the swap chain could not be resized to {}x{} ({})", width, height, FormatResult(l_Result));
            }

            if (AddBuffers())
            {
                TR_CORE_TRACE("D3D12: swap chain resized to {}x{}", m_Width, m_Height);
            }
        }

        void D3D12SwapChain::SetVSync(bool enabled)
        {
            m_Specification.VSync = enabled;
        }

        bool D3D12SwapChain::AddBuffers()
        {
            DXGI_SWAP_CHAIN_DESC1 l_Description{};
            m_SwapChain->GetDesc1(&l_Description);
            m_Width = l_Description.Width;
            m_Height = l_Description.Height;

            for (UINT it_Index = 0; it_Index < l_Description.BufferCount; ++it_Index)
            {
                ComPtr<ID3D12Resource>& l_Buffer = m_Buffers.emplace_back();
                const HRESULT l_Result = m_SwapChain->GetBuffer(it_Index, IID_PPV_ARGS(&l_Buffer));
                if (FAILED(l_Result))
                {
                    TR_CORE_ERROR("D3D12: swap chain buffer {} could not be read ({})", it_Index, FormatResult(l_Result));
                    m_Buffers.pop_back();

                    return false;
                }

                l_Buffer->SetName(L"Swap chain buffer");
                m_Textures.push_back(m_Device.AddSwapChainBuffer(l_Buffer.Get(), m_Format, l_Description.Format, m_Width, m_Height));
            }

            return true;
        }

        void D3D12SwapChain::RemoveBuffers()
        {
            for (TextureHandle it_Texture : m_Textures)
            {
                m_Device.RemoveSwapChainBuffer(it_Texture);
            }

            m_Textures.clear();
            m_Buffers.clear();
        }
    }
}