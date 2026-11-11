#include "Trinity/RHI/D3D12/D3D12Context.hpp"
#include "Trinity/RHI/D3D12/D3D12SwapChain.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            using Microsoft::WRL::ComPtr;

            // CPU-only heaps, so a large capacity costs little. A layered or mipped render target takes a view per mip of each layer
            constexpr std::uint32_t c_RenderTargetViewCapacity = 2048;
            constexpr std::uint32_t c_DepthStencilViewCapacity = 512;

            D3D12_PRIMITIVE_TOPOLOGY_TYPE ToTopologyType(PrimitiveTopology topology)
            {
                switch (topology)
                {
                    case PrimitiveTopology::LineList:
                    {
                        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                    }
                    case PrimitiveTopology::PointList:
                    {
                        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
                    }
                    default:
                    {
                        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                    }
                }
            }

            D3D_PRIMITIVE_TOPOLOGY ToD3DTopology(PrimitiveTopology topology)
            {
                switch (topology)
                {
                    case PrimitiveTopology::TriangleStrip:
                    {
                        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                    }
                    case PrimitiveTopology::LineList:
                    {
                        return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                    }
                    case PrimitiveTopology::PointList:
                    {
                        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
                    }
                    default:
                    {
                        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                    }
                }
            }

            D3D12_CULL_MODE ToCullMode(CullMode cull)
            {
                switch (cull)
                {
                    case CullMode::Front:
                    {
                        return D3D12_CULL_MODE_FRONT;
                    }
                    case CullMode::Back:
                    {
                        return D3D12_CULL_MODE_BACK;
                    }
                    default:
                    {
                        return D3D12_CULL_MODE_NONE;
                    }
                }
            }

            D3D12_COMPARISON_FUNC ToComparisonFunc(CompareOp compare)
            {
                switch (compare)
                {
                    case CompareOp::Never:
                    {
                        return D3D12_COMPARISON_FUNC_NEVER;
                    }
                    case CompareOp::Less:
                    {
                        return D3D12_COMPARISON_FUNC_LESS;
                    }
                    case CompareOp::Equal:
                    {
                        return D3D12_COMPARISON_FUNC_EQUAL;
                    }
                    case CompareOp::LessOrEqual:
                    {
                        return D3D12_COMPARISON_FUNC_LESS_EQUAL;
                    }
                    case CompareOp::Greater:
                    {
                        return D3D12_COMPARISON_FUNC_GREATER;
                    }
                    case CompareOp::NotEqual:
                    {
                        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
                    }
                    case CompareOp::GreaterOrEqual:
                    {
                        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
                    }
                    default:
                    {
                        return D3D12_COMPARISON_FUNC_ALWAYS;
                    }
                }
            }

            D3D12_STENCIL_OP ToStencilOp(StencilOp operation)
            {
                switch (operation)
                {
                    case StencilOp::Zero:
                    {
                        return D3D12_STENCIL_OP_ZERO;
                    }
                    case StencilOp::Replace:
                    {
                        return D3D12_STENCIL_OP_REPLACE;
                    }
                    case StencilOp::IncrementClamp:
                    {
                        return D3D12_STENCIL_OP_INCR_SAT;
                    }
                    case StencilOp::DecrementClamp:
                    {
                        return D3D12_STENCIL_OP_DECR_SAT;
                    }
                    default:
                    {
                        return D3D12_STENCIL_OP_KEEP;
                    }
                }
            }
        }

        DXGI_FORMAT ToDXGIFormat(Format format)
        {
            switch (format)
            {
                case Format::RGBA8Unorm:
                {
                    return DXGI_FORMAT_R8G8B8A8_UNORM;
                }
                case Format::RGBA8Srgb:
                {
                    return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                }
                case Format::BGRA8Unorm:
                {
                    return DXGI_FORMAT_B8G8R8A8_UNORM;
                }
                case Format::BGRA8Srgb:
                {
                    return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                }
                case Format::RGBA16Float:
                {
                    return DXGI_FORMAT_R16G16B16A16_FLOAT;
                }
                case Format::R11G11B10Float:
                {
                    return DXGI_FORMAT_R11G11B10_FLOAT;
                }
                case Format::R32Float:
                {
                    return DXGI_FORMAT_R32_FLOAT;
                }
                case Format::R32Uint:
                {
                    return DXGI_FORMAT_R32_UINT;
                }
                case Format::RG32Float:
                {
                    return DXGI_FORMAT_R32G32_FLOAT;
                }
                case Format::RGB32Float:
                {
                    return DXGI_FORMAT_R32G32B32_FLOAT;
                }
                case Format::RGBA32Float:
                {
                    return DXGI_FORMAT_R32G32B32A32_FLOAT;
                }
                case Format::D32Float:
                {
                    return DXGI_FORMAT_D32_FLOAT;
                }
                case Format::D32FloatS8:
                {
                    return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                }
                case Format::BC1Unorm:
                {
                    return DXGI_FORMAT_BC1_UNORM;
                }
                case Format::BC1Srgb:
                {
                    return DXGI_FORMAT_BC1_UNORM_SRGB;
                }
                case Format::BC3Unorm:
                {
                    return DXGI_FORMAT_BC3_UNORM;
                }
                case Format::BC3Srgb:
                {
                    return DXGI_FORMAT_BC3_UNORM_SRGB;
                }
                case Format::BC4Unorm:
                {
                    return DXGI_FORMAT_BC4_UNORM;
                }
                case Format::BC5Unorm:
                {
                    return DXGI_FORMAT_BC5_UNORM;
                }
                case Format::BC7Unorm:
                {
                    return DXGI_FORMAT_BC7_UNORM;
                }
                case Format::BC7Srgb:
                {
                    return DXGI_FORMAT_BC7_UNORM_SRGB;
                }
                default:
                {
                    return DXGI_FORMAT_UNKNOWN;
                }
            }
        }

        std::string FormatResult(HRESULT result)
        {
            return std::format("HRESULT 0x{:08X}", static_cast<std::uint32_t>(result));
        }

        std::wstring ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int l_Size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring l_Result(static_cast<std::size_t>(l_Size), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), l_Result.data(), l_Size);

            return l_Result;
        }

        void SetDebugName(ID3D12Resource* resource, std::string_view name)
        {
            if (!name.empty())
            {
                resource->SetName(ToWide(name).c_str());
            }
        }

        Scope<D3D12Context> D3D12Context::Create(const DeviceSpecification& specification, std::string& error)
        {
            Scope<D3D12Context> l_Device = CreateScope<D3D12Context>();
            if (!l_Device->Initialize(specification, error))
            {
                return nullptr;
            }

            return l_Device;
        }

        D3D12Context::D3D12Context() : m_Device(m_Factory), m_Allocator(m_Factory, m_Device), m_RootSignature(m_Device), m_Releases(m_RenderTargetViews, m_DepthStencilViews, m_ResourceHeap, m_SamplerHeap), m_CommandList(*this)
        {

        }

        D3D12Context::~D3D12Context()
        {
            if (m_Fence && m_Queue)
            {
                WaitForQueue();
            }

            m_UploadRing.Shutdown();

            if (m_Buffers.GetCount() != 0 || m_Textures.GetCount() != 0 || m_Pipelines.GetCount() != 0 || m_Samplers.GetCount() != 0 || m_QueryPools.GetCount() != 0)
            {
                TR_CORE_WARN("D3D12: the device was destroyed with {} buffer(s), {} texture(s), {} pipeline(s), {} sampler(s) and {} query pool(s) still alive", m_Buffers.GetCount(), m_Textures.GetCount(), m_Pipelines.GetCount(), m_Samplers.GetCount(), m_QueryPools.GetCount());
            }

            m_Releases.ReleaseAll();
            m_Buffers.ForEach([](D3D12Buffer& buffer) { buffer.Allocation.Reset(); });
            m_Textures.ForEach([](D3D12Texture& texture) { texture.Allocation.Reset(); });
            m_Pipelines.ForEach([](D3D12Pipeline& pipeline) { pipeline.State.Reset(); });
            m_QueryPools.ForEach([](D3D12QueryPool& pool) { pool.Heap.Reset(); });
            m_Allocator.Reset();
            m_RootSignature.Reset();
            m_ResourceHeap.Reset();
            m_SamplerHeap.Reset();
            m_RenderTargetViews.Reset();
            m_DepthStencilViews.Reset();
            m_GraphicsList.Reset();

            for (FrameContext& it_Frame : m_Frames)
            {
                it_Frame.Allocator.Reset();
            }

            m_Fence.Reset();
            m_Queue.Reset();
            if (m_FenceEvent != nullptr)
            {
                ::CloseHandle(m_FenceEvent);
            }

            const bool l_Report = m_Factory.IsValidationEnabled() && m_Device.GetHandle() != nullptr;

            m_Device.Reset();
            m_Factory.Reset();

            if (l_Report)
            {
                TR_CORE_INFO("D3D12 debug layer: {} warning(s) or error(s) while the device lived", m_Device.GetMessageCount());
                m_Device.ReportLiveObjects();
            }
        }

        bool D3D12Context::Initialize(const DeviceSpecification& specification, std::string& error)
        {
            if (!m_Factory.Initialize(specification, error) || !m_Device.Initialize(error))
            {
                return false;
            }

            m_Factory.LogRuntime();

            return m_Allocator.Initialize(error) && CreateFrames(error) && CreateBindless(error) && m_UploadRing.Initialize(*this, error);
        }

        bool D3D12Context::CreateFrames(std::string& error)
        {
            D3D12_COMMAND_QUEUE_DESC l_QueueDescription{};
            l_QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

            HRESULT l_Result = m_Device.GetHandle()->CreateCommandQueue(&l_QueueDescription, IID_PPV_ARGS(&m_Queue));
            for (std::size_t it_Index = 0; SUCCEEDED(l_Result) && it_Index < m_Frames.size(); ++it_Index)
            {
                l_Result = m_Device.GetHandle()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_Frames[it_Index].Allocator));
            }

            if (SUCCEEDED(l_Result))
            {
                l_Result = m_Device.GetHandle()->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_GraphicsList));
            }

            if (SUCCEEDED(l_Result))
            {
                l_Result = m_Device.GetHandle()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
            }

            if (FAILED(l_Result))
            {
                error = std::format("the command queue, command lists and fence could not be created on {} ({})", m_Device.GetInfo().AdapterName, FormatResult(l_Result));

                return false;
            }

            m_FenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (m_FenceEvent == nullptr || !m_RenderTargetViews.Initialize(m_Device.GetHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, c_RenderTargetViewCapacity, false) || !m_DepthStencilViews.Initialize(m_Device.GetHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, c_DepthStencilViewCapacity, false))
            {
                error = std::format("the fence event and view heaps could not be created on {}", m_Device.GetInfo().AdapterName);

                return false;
            }

            m_Queue->SetName(L"Trinity direct queue");
            m_GraphicsList->SetName(L"Trinity frame commands");

            return true;
        }

        // Shaders index the two heaps directly, so every pipeline shares one root signature that holds only the push constants
        bool D3D12Context::CreateBindless(std::string& error)
        {
            if (!m_ResourceHeap.Initialize(m_Device.GetHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, c_BindlessResourceCapacity, true) || !m_SamplerHeap.Initialize(m_Device.GetHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, c_BindlessSamplerCapacity, true))
            {
                error = std::format("the shader-visible descriptor heaps could not be created on {}", m_Device.GetInfo().AdapterName);

                return false;
            }

            if (!m_RootSignature.Initialize(error))
            {
                return false;
            }

            m_ResourceHeap.GetHeap()->SetName(L"Trinity bindless resources");
            m_SamplerHeap.GetHeap()->SetName(L"Trinity bindless samplers");

            return true;
        }

        BufferHandle D3D12Context::CreateBuffer(const BufferDescription& description)
        {
            std::optional<D3D12Buffer> l_Buffer = D3D12Buffer::Create(m_Device, m_Allocator, m_ResourceHeap, description);
            if (!l_Buffer)
            {
                return {};
            }

            return m_Buffers.Add(std::move(*l_Buffer));
        }

        void D3D12Context::DestroyBuffer(BufferHandle buffer)
        {
            if (!buffer)
            {
                return;
            }

            std::optional<D3D12Buffer> l_Buffer = m_Buffers.Remove(buffer);
            TR_CORE_ASSERT(l_Buffer.has_value(), "DestroyBuffer on a buffer that was already destroyed.");

            if (l_Buffer)
            {
                m_Releases.Push(l_Buffer->ToRelease());
            }
        }

        std::span<std::byte> D3D12Context::GetMappedData(BufferHandle buffer)
        {
            const D3D12Buffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetMappedData on a destroyed or invalid buffer.");

            if (l_Buffer == nullptr || l_Buffer->Mapped == nullptr)
            {
                return {};
            }

            return { l_Buffer->Mapped, static_cast<std::size_t>(l_Buffer->Size) };
        }

        bool D3D12Context::IsFormatSupported(Format format, TextureUsage usage, std::uint32_t sampleCount) const
        {
            return D3D12Texture::IsFormatSupported(m_Device, format, usage, sampleCount);
        }

        TextureHandle D3D12Context::CreateTexture(const TextureDescription& description)
        {
            TR_CORE_ASSERT(description.Width != 0 && description.Height != 0 && description.MipLevels != 0, "Texture '{}' has a zero size or no mips.", description.DebugName);
            TR_CORE_ASSERT(description.TextureFormat != Format::Unknown, "Texture '{}' has no format.", description.DebugName);

            if (!CanCreateTexture(description))
            {
                return {};
            }

            std::optional<D3D12Texture> l_Texture = D3D12Texture::Create(m_Device, m_Allocator, m_RenderTargetViews, m_DepthStencilViews, m_ResourceHeap, description);
            if (!l_Texture)
            {
                return {};
            }

            return m_Textures.Add(std::move(*l_Texture));
        }

        void D3D12Context::DestroyTexture(TextureHandle texture)
        {
            if (!texture)
            {
                return;
            }

            std::optional<D3D12Texture> l_Texture = m_Textures.Remove(texture);
            TR_CORE_ASSERT(l_Texture.has_value(), "DestroyTexture on a texture that was already destroyed.");

            if (l_Texture)
            {
                m_Releases.Push(l_Texture->ToRelease());
            }
        }

        TextureHandle D3D12Context::AddSwapChainBuffer(ID3D12Resource* resource, Format format, DXGI_FORMAT resourceFormat, std::uint32_t width, std::uint32_t height)
        {
            return m_Textures.Add(D3D12Texture::FromSwapChainBuffer(m_Device, m_RenderTargetViews, m_DepthStencilViews, resource, format, resourceFormat, width, height));
        }

        // The swap chain waits for the device to be idle first, so the view is free at once
        void D3D12Context::RemoveSwapChainBuffer(TextureHandle texture)
        {
            const std::optional<D3D12Texture> l_Texture = m_Textures.Remove(texture);
            if (l_Texture)
            {
                m_RenderTargetViews.Free(l_Texture->RenderTargetViews.front());
            }
        }

        void D3D12Context::WaitForFence(std::uint64_t value)
        {
            if (m_Fence->GetCompletedValue() < value && SUCCEEDED(m_Fence->SetEventOnCompletion(value, m_FenceEvent)))
            {
                ::WaitForSingleObject(m_FenceEvent, INFINITE);
            }
        }

        // Presents go on the queue after the frame's signal, so waiting for that signal alone can leave a swap chain buffer in use. A new signal comes after them
        void D3D12Context::WaitForQueue()
        {
            m_Queue->Signal(m_Fence.Get(), ++m_FenceValue);
            WaitForFence(m_FenceValue);
        }

        std::uint32_t D3D12Context::GetShaderResourceIndex(BufferHandle buffer)
        {
            const D3D12Buffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetShaderResourceIndex on a destroyed or invalid buffer.");

            return l_Buffer != nullptr ? l_Buffer->ShaderResourceIndex : c_NoBindlessIndex;
        }

        std::uint32_t D3D12Context::GetUnorderedAccessIndex(BufferHandle buffer)
        {
            const D3D12Buffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetUnorderedAccessIndex on a destroyed or invalid buffer.");

            return l_Buffer != nullptr ? l_Buffer->UnorderedAccessIndex : c_NoBindlessIndex;
        }

        std::uint32_t D3D12Context::GetShaderResourceIndex(TextureHandle texture)
        {
            const D3D12Texture* l_Texture = m_Textures.Get(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "GetShaderResourceIndex on a destroyed or invalid texture.");

            return l_Texture != nullptr ? l_Texture->ShaderResourceIndex : c_NoBindlessIndex;
        }

        std::uint32_t D3D12Context::GetUnorderedAccessIndex(TextureHandle texture)
        {
            const D3D12Texture* l_Texture = m_Textures.Get(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "GetUnorderedAccessIndex on a destroyed or invalid texture.");

            return l_Texture != nullptr ? l_Texture->UnorderedAccessIndex : c_NoBindlessIndex;
        }

        SamplerHandle D3D12Context::CreateSampler(const SamplerDescription& description)
        {
            const std::optional<D3D12Sampler> l_Sampler = D3D12Sampler::Create(m_Device, m_SamplerHeap, description);
            if (!l_Sampler)
            {
                return {};
            }

            return m_Samplers.Add(*l_Sampler);
        }

        void D3D12Context::DestroySampler(SamplerHandle sampler)
        {
            if (!sampler)
            {
                return;
            }

            const std::optional<D3D12Sampler> l_Sampler = m_Samplers.Remove(sampler);
            TR_CORE_ASSERT(l_Sampler.has_value(), "DestroySampler on a sampler that was already destroyed.");

            if (l_Sampler)
            {
                m_Releases.Push(l_Sampler->ToRelease());
            }
        }

        std::uint32_t D3D12Context::GetSamplerIndex(SamplerHandle sampler)
        {
            const D3D12Sampler* l_Sampler = m_Samplers.Get(sampler);
            TR_CORE_ASSERT(l_Sampler != nullptr, "GetSamplerIndex on a destroyed or invalid sampler.");

            return l_Sampler != nullptr ? l_Sampler->Index : c_NoBindlessIndex;
        }

        PipelineHandle D3D12Context::CreateGraphicsPipeline(const GraphicsPipelineDescription& description)
        {
            TR_CORE_ASSERT(!description.VertexShader.Code.empty() && !description.PixelShader.Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);
            TR_CORE_ASSERT(description.ColorFormats.size() <= c_MaxColorAttachments, "Pipeline '{}' has too many color formats.", description.DebugName);
            TR_CORE_ASSERT(!description.StencilTest || HasStencil(description.DepthFormat), "Pipeline '{}' tests stencil without a depth format that has it.", description.DebugName);

            D3D12_GRAPHICS_PIPELINE_STATE_DESC l_Description{};
            l_Description.pRootSignature = m_RootSignature.GetHandle();
            l_Description.VS = { description.VertexShader.Code.data(), description.VertexShader.Code.size() };
            l_Description.PS = { description.PixelShader.Code.data(), description.PixelShader.Code.size() };
            l_Description.SampleMask = UINT_MAX;
            l_Description.PrimitiveTopologyType = ToTopologyType(description.Topology);
            l_Description.SampleDesc.Count = description.SampleCount;

            // An integer target is never blended, so blending is set for each target
            l_Description.NumRenderTargets = static_cast<UINT>(std::min<std::size_t>(description.ColorFormats.size(), c_MaxColorAttachments));
            l_Description.BlendState.IndependentBlendEnable = TRUE;
            for (UINT it_Target = 0; it_Target < l_Description.NumRenderTargets; ++it_Target)
            {
                D3D12_RENDER_TARGET_BLEND_DESC& l_Blend = l_Description.BlendState.RenderTarget[it_Target];
                l_Blend.BlendEnable = description.AlphaBlend && !IsIntegerFormat(description.ColorFormats[it_Target]) ? TRUE : FALSE;
                l_Blend.SrcBlend = description.PremultipliedAlpha ? D3D12_BLEND_ONE : D3D12_BLEND_SRC_ALPHA;
                l_Blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                l_Blend.BlendOp = D3D12_BLEND_OP_ADD;
                l_Blend.SrcBlendAlpha = D3D12_BLEND_ONE;
                l_Blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                l_Blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                l_Blend.LogicOp = D3D12_LOGIC_OP_NOOP;
                l_Blend.RenderTargetWriteMask = description.ColorWrite ? D3D12_COLOR_WRITE_ENABLE_ALL : 0;

                l_Description.RTVFormats[it_Target] = ToDXGIFormat(description.ColorFormats[it_Target]);
            }

            l_Description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            l_Description.RasterizerState.CullMode = ToCullMode(description.Cull);
            l_Description.RasterizerState.FrontCounterClockwise = description.FrontCounterClockwise ? TRUE : FALSE;
            l_Description.RasterizerState.DepthBias = description.DepthBiasConstant;
            l_Description.RasterizerState.SlopeScaledDepthBias = description.DepthBiasSlope;
            l_Description.RasterizerState.DepthBiasClamp = description.DepthBiasClamp;

            // D3D12 always clamps depth to the viewport's range, so turning clipping off is what Vulkan's depth clamp does
            l_Description.RasterizerState.DepthClipEnable = description.DepthClamp ? FALSE : TRUE;

            // Vulkan never writes depth without the depth test, so neither does this
            l_Description.DepthStencilState.DepthEnable = description.DepthTest ? TRUE : FALSE;
            l_Description.DepthStencilState.DepthWriteMask = description.DepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            l_Description.DepthStencilState.DepthFunc = ToComparisonFunc(description.DepthCompare);
            l_Description.DepthStencilState.StencilEnable = description.StencilTest ? TRUE : FALSE;
            l_Description.DepthStencilState.StencilReadMask = description.StencilReadMask;
            l_Description.DepthStencilState.StencilWriteMask = description.StencilWriteMask;
            l_Description.DepthStencilState.FrontFace = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, ToStencilOp(description.StencilPass), ToComparisonFunc(description.StencilCompare) };
            l_Description.DepthStencilState.BackFace = l_Description.DepthStencilState.FrontFace;
            l_Description.DSVFormat = description.DepthFormat != Format::Unknown ? ToDXGIFormat(description.DepthFormat) : DXGI_FORMAT_UNKNOWN;

            D3D12Pipeline l_Pipeline;
            l_Pipeline.Topology = ToD3DTopology(description.Topology);
            const HRESULT l_Result = m_Device.GetHandle()->CreateGraphicsPipelineState(&l_Description, IID_PPV_ARGS(&l_Pipeline.State));
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: pipeline '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            if (!description.DebugName.empty())
            {
                l_Pipeline.State->SetName(ToWide(description.DebugName).c_str());
            }

            return m_Pipelines.Add(std::move(l_Pipeline));
        }

        QueryPoolHandle D3D12Context::CreateQueryPool(const QueryPoolDescription& description)
        {
            TR_CORE_ASSERT(description.Count != 0, "Query pool '{}' has no queries.", description.DebugName);

            D3D12_QUERY_HEAP_DESC l_Description{};
            l_Description.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            l_Description.Count = description.Count;

            D3D12QueryPool l_Pool;
            l_Pool.Count = description.Count;
            const HRESULT l_Result = m_Device.GetHandle()->CreateQueryHeap(&l_Description, IID_PPV_ARGS(&l_Pool.Heap));
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: query pool '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            if (!description.DebugName.empty())
            {
                l_Pool.Heap->SetName(ToWide(description.DebugName).c_str());
            }

            return m_QueryPools.Add(std::move(l_Pool));
        }

        void D3D12Context::DestroyQueryPool(QueryPoolHandle pool)
        {
            if (!pool)
            {
                return;
            }

            std::optional<D3D12QueryPool> l_Pool = m_QueryPools.Remove(pool);
            TR_CORE_ASSERT(l_Pool.has_value(), "DestroyQueryPool on a query pool that was already destroyed.");

            if (l_Pool)
            {
                D3D12Release l_Release;
                l_Release.QueryHeap = std::move(l_Pool->Heap);
                m_Releases.Push(std::move(l_Release));
            }
        }

        std::uint64_t D3D12Context::GetTimestampFrequency() const
        {
            UINT64 l_Frequency = 0;

            return m_Queue && SUCCEEDED(m_Queue->GetTimestampFrequency(&l_Frequency)) ? l_Frequency : 0;
        }

        PipelineHandle D3D12Context::CreateComputePipeline(const ComputePipelineDescription& description)
        {
            TR_CORE_ASSERT(!description.ComputeShader.Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);

            D3D12_COMPUTE_PIPELINE_STATE_DESC l_Description{};
            l_Description.pRootSignature = m_RootSignature.GetHandle();
            l_Description.CS = { description.ComputeShader.Code.data(), description.ComputeShader.Code.size() };

            D3D12Pipeline l_Pipeline;
            l_Pipeline.Compute = true;
            const HRESULT l_Result = m_Device.GetHandle()->CreateComputePipelineState(&l_Description, IID_PPV_ARGS(&l_Pipeline.State));
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: pipeline '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            if (!description.DebugName.empty())
            {
                l_Pipeline.State->SetName(ToWide(description.DebugName).c_str());
            }

            return m_Pipelines.Add(std::move(l_Pipeline));
        }

        void D3D12Context::DestroyPipeline(PipelineHandle pipeline)
        {
            if (!pipeline)
            {
                return;
            }

            std::optional<D3D12Pipeline> l_Pipeline = m_Pipelines.Remove(pipeline);
            TR_CORE_ASSERT(l_Pipeline.has_value(), "DestroyPipeline on a pipeline that was already destroyed.");

            if (l_Pipeline)
            {
                D3D12Release l_Release;
                l_Release.Pipeline = std::move(l_Pipeline->State);
                m_Releases.Push(std::move(l_Release));
            }
        }

        Scope<SwapChain> D3D12Context::CreateSwapChain(const SwapChainSpecification& specification)
        {
            return D3D12SwapChain::Create(*this, specification);
        }

        // Waits until the frame c_FramesInFlight before this one, which used the same command allocator, has finished on the GPU
        CommandList& D3D12Context::BeginFrame()
        {
            TR_CORE_ASSERT(!m_InFrame, "BeginFrame was called twice without EndFrame.");

            FrameContext& l_Frame = m_Frames[m_FrameNumber % c_FramesInFlight];
            WaitForFence(l_Frame.CompletionValue);

            m_InFrame = true;
            m_Releases.BeginFrame();
            m_UploadRing.BeginFrame();

            l_Frame.Allocator->Reset();
            m_GraphicsList->Reset(l_Frame.Allocator.Get(), nullptr);
            m_CommandList.Begin(m_GraphicsList.Get());

            return m_CommandList;
        }

        void D3D12Context::EndFrame()
        {
            TR_CORE_ASSERT(m_InFrame, "EndFrame without BeginFrame.");

            FrameContext& l_Frame = m_Frames[m_FrameNumber % c_FramesInFlight];
            m_UploadRing.EndFrame();
            m_CommandList.End();

            ID3D12CommandList* const l_Lists[] = { m_GraphicsList.Get() };
            m_Queue->ExecuteCommandLists(1, l_Lists);

            l_Frame.CompletionValue = ++m_FenceValue;
            m_Queue->Signal(m_Fence.Get(), l_Frame.CompletionValue);

            ++m_FrameNumber;
            m_Releases.EndFrame();
            m_InFrame = false;
        }

        void D3D12Context::WaitIdle()
        {
            WaitForQueue();
            m_Releases.ReleaseIdle();
        }
    }
}