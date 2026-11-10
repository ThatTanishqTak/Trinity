#include "Trinity/RHI/D3D12/D3D12Context.hpp"
#include "Trinity/RHI/D3D12/D3D12SwapChain.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"

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

            D3D12_HEAP_TYPE ToHeapType(MemoryType memory)
            {
                switch (memory)
                {
                    case MemoryType::Upload:
                    {
                        return D3D12_HEAP_TYPE_UPLOAD;
                    }
                    case MemoryType::Readback:
                    {
                        return D3D12_HEAP_TYPE_READBACK;
                    }
                    default:
                    {
                        return D3D12_HEAP_TYPE_DEFAULT;
                    }
                }
            }

            // CPU-only heaps, so a large capacity costs little. A layered or mipped render target takes a view per mip of each layer
            constexpr std::uint32_t c_RenderTargetViewCapacity = 2048;
            constexpr std::uint32_t c_DepthStencilViewCapacity = 512;

            D3D12_RESOURCE_FLAGS ToResourceFlags(TextureUsage usage)
            {
                D3D12_RESOURCE_FLAGS l_Flags = D3D12_RESOURCE_FLAG_NONE;
                if (HasFlag(usage, TextureUsage::UnorderedAccess))
                {
                    l_Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                }

                if (HasFlag(usage, TextureUsage::RenderTarget))
                {
                    l_Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                }

                if (HasFlag(usage, TextureUsage::DepthStencil))
                {
                    l_Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
                    if (!HasFlag(usage, TextureUsage::ShaderResource))
                    {
                        l_Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
                    }
                }

                return l_Flags;
            }

            void SetDebugName(ID3D12Resource* resource, std::string_view name)
            {
                if (!name.empty())
                {
                    resource->SetName(ToWide(name).c_str());
                }
            }

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

            D3D12_FILTER_TYPE ToFilterType(Filter filter)
            {
                return filter == Filter::Linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
            }

            // What D3D12_ENCODE_BASIC_FILTER builds, without its C-style casts
            D3D12_FILTER ToD3D12Filter(const SamplerDescription& description)
            {
                const UINT l_Min = static_cast<UINT>(ToFilterType(description.MinFilter)) << D3D12_MIN_FILTER_SHIFT;
                const UINT l_Mag = static_cast<UINT>(ToFilterType(description.MagFilter)) << D3D12_MAG_FILTER_SHIFT;
                const UINT l_Mip = static_cast<UINT>(ToFilterType(description.MipFilter)) << D3D12_MIP_FILTER_SHIFT;
                const UINT l_Reduction = static_cast<UINT>(D3D12_FILTER_REDUCTION_TYPE_STANDARD) << D3D12_FILTER_REDUCTION_TYPE_SHIFT;

                return static_cast<D3D12_FILTER>(l_Min | l_Mag | l_Mip | l_Reduction);
            }

            D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(AddressMode address)
            {
                switch (address)
                {
                    case AddressMode::MirroredRepeat:
                    {
                        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
                    }
                    case AddressMode::ClampToEdge:
                    {
                        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    }
                    default:
                    {
                        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
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

        // Buffers always start in the undefined layout under enhanced barriers. Upload and Readback heaps are coherent and stay mapped
        BufferHandle D3D12Context::CreateBuffer(const BufferDescription& description)
        {
            TR_CORE_ASSERT(description.Size != 0, "Buffer '{}' has no size.", description.DebugName);

            D3D12MA::ALLOCATION_DESC l_Allocation{};
            l_Allocation.HeapType = ToHeapType(description.Memory);

            D3D12_RESOURCE_DESC1 l_Resource{};
            l_Resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            l_Resource.Width = description.Size;
            l_Resource.Height = 1;
            l_Resource.DepthOrArraySize = 1;
            l_Resource.MipLevels = 1;
            l_Resource.Format = DXGI_FORMAT_UNKNOWN;
            l_Resource.SampleDesc.Count = 1;
            l_Resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            l_Resource.Flags = HasFlag(description.Usage, BufferUsage::UnorderedAccess) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

            D3D12Buffer l_Buffer;
            HRESULT l_Result = m_Allocator.GetHandle()->CreateResource3(&l_Allocation, &l_Resource, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, 0, nullptr, &l_Buffer.Allocation, IID_NULL, nullptr);
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: buffer '{}' of {} could not be created ({})", description.DebugName, Memory::FormatBytes(description.Size), FormatResult(l_Result));

                return {};
            }

            ID3D12Resource* l_Native = l_Buffer.Allocation->GetResource();
            if (description.Memory != MemoryType::GPU)
            {
                // The CPU only writes Upload buffers, so mapping one says nothing will be read
                const D3D12_RANGE l_NoRead{ 0, 0 };
                void* l_Mapped = nullptr;
                l_Result = l_Native->Map(0, description.Memory == MemoryType::Upload ? &l_NoRead : nullptr, &l_Mapped);
                if (FAILED(l_Result))
                {
                    TR_CORE_ERROR("D3D12: buffer '{}' could not be mapped ({})", description.DebugName, FormatResult(l_Result));

                    return {};
                }

                l_Buffer.Mapped = static_cast<std::byte*>(l_Mapped);
            }

            l_Buffer.Size = description.Size;
            SetDebugName(l_Native, description.DebugName);
            CreateShaderViews(l_Buffer, description.Usage);

            return m_Buffers.Add(std::move(l_Buffer));
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
                D3D12Release l_Release;
                l_Release.Allocation = std::move(l_Buffer->Allocation);
                l_Release.ShaderResourceIndex = l_Buffer->ShaderResourceIndex;
                l_Release.UnorderedAccessIndex = l_Buffer->UnorderedAccessIndex;
                m_Releases.Push(std::move(l_Release));
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

        // Every usage asked for must be in the format's support. Shaders read a depth texture through an R32_FLOAT view, so that is the format to check for reading
        bool D3D12Context::IsFormatSupported(Format format, TextureUsage usage, std::uint32_t sampleCount) const
        {
            const DXGI_FORMAT l_Format = ToDXGIFormat(format);
            if (l_Format == DXGI_FORMAT_UNKNOWN || (HasStencil(format) && usage != TextureUsage::DepthStencil))
            {
                return false;
            }

            const auto a_Query = [this](DXGI_FORMAT queried) -> D3D12_FORMAT_SUPPORT1
            {
                D3D12_FEATURE_DATA_FORMAT_SUPPORT l_Data{ queried, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
                if (FAILED(m_Device.GetHandle()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &l_Data, sizeof(l_Data))))
                {
                    return D3D12_FORMAT_SUPPORT1_NONE;
                }

                return l_Data.Support1;
            };

            const D3D12_FORMAT_SUPPORT1 l_Support = a_Query(l_Format);
            const D3D12_FORMAT_SUPPORT1 l_ReadSupport = format == Format::D32Float ? a_Query(DXGI_FORMAT_R32_FLOAT) : l_Support;

            const auto a_Has = [](D3D12_FORMAT_SUPPORT1 support, D3D12_FORMAT_SUPPORT1 needed)
            {
                return (support & needed) == needed;
            };

            if (!a_Has(l_Support, D3D12_FORMAT_SUPPORT1_TEXTURE2D))
            {
                return false;
            }

            if (HasFlag(usage, TextureUsage::ShaderResource) && !a_Has(l_ReadSupport, D3D12_FORMAT_SUPPORT1_SHADER_LOAD))
            {
                return false;
            }

            if (HasFlag(usage, TextureUsage::UnorderedAccess) && !a_Has(l_Support, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW))
            {
                return false;
            }

            if (HasFlag(usage, TextureUsage::RenderTarget) && !a_Has(l_Support, D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
            {
                return false;
            }

            if (HasFlag(usage, TextureUsage::DepthStencil) && !a_Has(l_Support, D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL))
            {
                return false;
            }

            if (sampleCount == 1)
            {
                return true;
            }

            const bool l_Attachment = HasFlag(usage, TextureUsage::RenderTarget) || HasFlag(usage, TextureUsage::DepthStencil);
            if ((l_Attachment && !a_Has(l_Support, D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET)) || (HasFlag(usage, TextureUsage::ShaderResource) && !a_Has(l_ReadSupport, D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD)))
            {
                return false;
            }

            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS l_Levels{ l_Format, sampleCount, D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE, 0 };

            return SUCCEEDED(m_Device.GetHandle()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &l_Levels, sizeof(l_Levels))) && l_Levels.NumQualityLevels > 0;
        }

        // A depth texture that shaders also read is typeless, so a shader view can read it as R32_FLOAT
        TextureHandle D3D12Context::CreateTexture(const TextureDescription& description)
        {
            TR_CORE_ASSERT(description.Width != 0 && description.Height != 0 && description.MipLevels != 0, "Texture '{}' has a zero size or no mips.", description.DebugName);
            TR_CORE_ASSERT(description.TextureFormat != Format::Unknown, "Texture '{}' has no format.", description.DebugName);

            if (!CanCreateTexture(description))
            {
                return {};
            }

            const bool l_TypelessDepth = description.TextureFormat == Format::D32Float && HasFlag(description.Usage, TextureUsage::ShaderResource);

            D3D12MA::ALLOCATION_DESC l_Allocation{};
            l_Allocation.HeapType = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC1 l_Resource{};
            l_Resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            l_Resource.Width = description.Width;
            l_Resource.Height = description.Height;
            l_Resource.DepthOrArraySize = static_cast<UINT16>(description.ArrayLayers);
            l_Resource.MipLevels = static_cast<UINT16>(description.MipLevels);
            l_Resource.Format = l_TypelessDepth ? DXGI_FORMAT_R32_TYPELESS : ToDXGIFormat(description.TextureFormat);
            l_Resource.SampleDesc.Count = description.SampleCount;
            l_Resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            l_Resource.Flags = ToResourceFlags(description.Usage);

            D3D12_CLEAR_VALUE l_ClearValue{};
            const D3D12_CLEAR_VALUE* l_OptimizedClear = nullptr;
            if (description.OptimizedClear && HasFlag(description.Usage, TextureUsage::RenderTarget))
            {
                l_ClearValue.Format = ToDXGIFormat(description.TextureFormat);
                std::ranges::copy(description.ClearColor, l_ClearValue.Color);
                l_OptimizedClear = &l_ClearValue;
            }
            else if (description.OptimizedClear && HasFlag(description.Usage, TextureUsage::DepthStencil))
            {
                l_ClearValue.Format = ToDXGIFormat(description.TextureFormat);
                l_ClearValue.DepthStencil.Depth = description.ClearDepth;
                l_OptimizedClear = &l_ClearValue;
            }

            D3D12Texture l_Texture;
            const HRESULT l_Result = m_Allocator.GetHandle()->CreateResource3(&l_Allocation, &l_Resource, D3D12_BARRIER_LAYOUT_COMMON, l_OptimizedClear, 0, nullptr, &l_Texture.Allocation, IID_NULL, nullptr);
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: texture '{}' ({}x{} {}) could not be created ({})", description.DebugName, description.Width, description.Height, ToString(description.TextureFormat), FormatResult(l_Result));

                return {};
            }

            l_Texture.Resource = l_Texture.Allocation->GetResource();
            l_Texture.TextureFormat = description.TextureFormat;
            l_Texture.ResourceFormat = l_Resource.Format;
            l_Texture.Dimension = description.Dimension;
            l_Texture.Width = description.Width;
            l_Texture.Height = description.Height;
            l_Texture.MipLevels = description.MipLevels;
            l_Texture.ArrayLayers = description.ArrayLayers;
            l_Texture.SampleCount = description.SampleCount;
            SetDebugName(l_Texture.Resource, description.DebugName);

            if (HasFlag(description.Usage, TextureUsage::RenderTarget) || HasFlag(description.Usage, TextureUsage::DepthStencil))
            {
                CreateViews(l_Texture);
            }

            CreateShaderViews(l_Texture, description.Usage);

            return m_Textures.Add(std::move(l_Texture));
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
                D3D12Release l_Release;
                l_Release.Allocation = std::move(l_Texture->Allocation);
                l_Release.RenderTargetViews = std::move(l_Texture->RenderTargetViews);
                l_Release.DepthStencilViews = std::move(l_Texture->DepthStencilViews);
                l_Release.ShaderResourceIndex = l_Texture->ShaderResourceIndex;
                l_Release.UnorderedAccessIndex = l_Texture->UnorderedAccessIndex;
                m_Releases.Push(std::move(l_Release));
            }
        }

        // A view per mip of each layer, as a single 2D texture for a Texture2D and as one slice of an array otherwise
        void D3D12Context::CreateViews(D3D12Texture& texture)
        {
            const bool l_Depth = IsDepthFormat(texture.TextureFormat);
            const bool l_Array = texture.Dimension != TextureDimension::Texture2D;
            const bool l_Multisampled = texture.SampleCount > 1;
            D3D12DescriptorIndices& l_Views = l_Depth ? texture.DepthStencilViews : texture.RenderTargetViews;
            D3D12DescriptorHeap& l_Heap = l_Depth ? m_DepthStencilViews : m_RenderTargetViews;

            l_Views.reserve(std::size_t{ texture.MipLevels } * texture.ArrayLayers);
            for (std::uint32_t it_Layer = 0; it_Layer < texture.ArrayLayers; ++it_Layer)
            {
                for (std::uint32_t it_Mip = 0; it_Mip < texture.MipLevels; ++it_Mip)
                {
                    const std::uint32_t l_View = l_Heap.Allocate();
                    TR_CORE_ASSERT(l_View != c_NoDescriptor, "Out of {} views.", l_Depth ? "depth stencil" : "render target");
                    l_Views.push_back(l_View);
                    if (l_View == c_NoDescriptor)
                    {
                        continue;
                    }

                    if (l_Depth)
                    {
                        D3D12_DEPTH_STENCIL_VIEW_DESC l_Description{};
                        l_Description.Format = ToDXGIFormat(texture.TextureFormat);
                        l_Description.ViewDimension = l_Array ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY : l_Multisampled ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
                        if (l_Array)
                        {
                            l_Description.Texture2DArray = { it_Mip, it_Layer, 1 };
                        }
                        else
                        {
                            l_Description.Texture2D.MipSlice = it_Mip;
                        }

                        m_Device.GetHandle()->CreateDepthStencilView(texture.Resource, &l_Description, l_Heap.GetHandle(l_View));
                    }
                    else
                    {
                        D3D12_RENDER_TARGET_VIEW_DESC l_Description{};
                        l_Description.Format = ToDXGIFormat(texture.TextureFormat);
                        l_Description.ViewDimension = l_Array ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY : l_Multisampled ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
                        if (l_Array)
                        {
                            l_Description.Texture2DArray = { it_Mip, it_Layer, 1, 0 };
                        }
                        else
                        {
                            l_Description.Texture2D.MipSlice = it_Mip;
                        }

                        m_Device.GetHandle()->CreateRenderTargetView(texture.Resource, &l_Description, l_Heap.GetHandle(l_View));
                    }
                }
            }
        }

        // Buffers get raw views, which is what a ByteAddressBuffer reads and writes
        void D3D12Context::CreateShaderViews(D3D12Buffer& buffer, BufferUsage usage)
        {
            ID3D12Resource* l_Resource = buffer.Allocation->GetResource();
            const UINT l_Elements = static_cast<UINT>(buffer.Size / 4);

            if (HasFlag(usage, BufferUsage::ShaderResource))
            {
                buffer.ShaderResourceIndex = m_ResourceHeap.Allocate();
                TR_CORE_ASSERT(buffer.ShaderResourceIndex != c_NoDescriptor, "Out of bindless resource indices.");
                if (buffer.ShaderResourceIndex != c_NoDescriptor)
                {
                    D3D12_SHADER_RESOURCE_VIEW_DESC l_View{};
                    l_View.Format = DXGI_FORMAT_R32_TYPELESS;
                    l_View.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                    l_View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    l_View.Buffer.NumElements = l_Elements;
                    l_View.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
                    m_Device.GetHandle()->CreateShaderResourceView(l_Resource, &l_View, m_ResourceHeap.GetHandle(buffer.ShaderResourceIndex));
                }
            }

            if (HasFlag(usage, BufferUsage::UnorderedAccess))
            {
                buffer.UnorderedAccessIndex = m_ResourceHeap.Allocate();
                TR_CORE_ASSERT(buffer.UnorderedAccessIndex != c_NoDescriptor, "Out of bindless resource indices.");
                if (buffer.UnorderedAccessIndex != c_NoDescriptor)
                {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC l_View{};
                    l_View.Format = DXGI_FORMAT_R32_TYPELESS;
                    l_View.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                    l_View.Buffer.NumElements = l_Elements;
                    l_View.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                    m_Device.GetHandle()->CreateUnorderedAccessView(l_Resource, nullptr, &l_View, m_ResourceHeap.GetHandle(buffer.UnorderedAccessIndex));
                }
            }
        }

        // The shader resource view covers every mip and layer, and the unordered access view mip 0 of every layer, a cube's faces as an array since there are no cube unordered access views
        void D3D12Context::CreateShaderViews(D3D12Texture& texture, TextureUsage usage)
        {
            if (HasFlag(usage, TextureUsage::ShaderResource))
            {
                texture.ShaderResourceIndex = m_ResourceHeap.Allocate();
                TR_CORE_ASSERT(texture.ShaderResourceIndex != c_NoDescriptor, "Out of bindless resource indices.");
                if (texture.ShaderResourceIndex != c_NoDescriptor)
                {
                    D3D12_SHADER_RESOURCE_VIEW_DESC l_View{};
                    l_View.Format = texture.ResourceFormat == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_R32_FLOAT : texture.ResourceFormat;
                    l_View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    switch (texture.Dimension)
                    {
                        case TextureDimension::Texture2DArray:
                        {
                            l_View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                            l_View.Texture2DArray.MipLevels = texture.MipLevels;
                            l_View.Texture2DArray.ArraySize = texture.ArrayLayers;

                            break;
                        }
                        case TextureDimension::TextureCube:
                        {
                            l_View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                            l_View.TextureCube.MipLevels = texture.MipLevels;

                            break;
                        }
                        default:
                        {
                            l_View.ViewDimension = texture.SampleCount > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
                            l_View.Texture2D.MipLevels = texture.MipLevels;

                            break;
                        }
                    }
                    m_Device.GetHandle()->CreateShaderResourceView(texture.Resource, &l_View, m_ResourceHeap.GetHandle(texture.ShaderResourceIndex));
                }
            }

            if (HasFlag(usage, TextureUsage::UnorderedAccess))
            {
                texture.UnorderedAccessIndex = m_ResourceHeap.Allocate();
                TR_CORE_ASSERT(texture.UnorderedAccessIndex != c_NoDescriptor, "Out of bindless resource indices.");
                if (texture.UnorderedAccessIndex != c_NoDescriptor)
                {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC l_View{};
                    l_View.Format = texture.ResourceFormat;
                    l_View.ViewDimension = texture.Dimension == TextureDimension::Texture2D ? D3D12_UAV_DIMENSION_TEXTURE2D : D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                    if (texture.Dimension != TextureDimension::Texture2D)
                    {
                        l_View.Texture2DArray.ArraySize = texture.ArrayLayers;
                    }

                    m_Device.GetHandle()->CreateUnorderedAccessView(texture.Resource, nullptr, &l_View, m_ResourceHeap.GetHandle(texture.UnorderedAccessIndex));
                }
            }
        }

        TextureHandle D3D12Context::AddSwapChainBuffer(ID3D12Resource* resource, Format format, DXGI_FORMAT resourceFormat, std::uint32_t width, std::uint32_t height)
        {
            D3D12Texture l_Texture;
            l_Texture.Resource = resource;
            l_Texture.TextureFormat = format;
            l_Texture.ResourceFormat = resourceFormat;
            l_Texture.Width = width;
            l_Texture.Height = height;
            l_Texture.MipLevels = 1;
            l_Texture.ArrayLayers = 1;
            CreateViews(l_Texture);

            return m_Textures.Add(std::move(l_Texture));
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

        // The comparison function only matters to comparison filters, and NEVER is valid on every runtime
        SamplerHandle D3D12Context::CreateSampler(const SamplerDescription& description)
        {
            D3D12Sampler l_Sampler;
            l_Sampler.Index = m_SamplerHeap.Allocate();
            if (l_Sampler.Index == c_NoDescriptor)
            {
                TR_CORE_ERROR("D3D12: sampler '{}' could not be created, since all {} bindless sampler indices are in use", description.DebugName, c_BindlessSamplerCapacity);

                return {};
            }

            D3D12_SAMPLER_DESC l_Description{};
            l_Description.Filter = ToD3D12Filter(description);
            l_Description.AddressU = ToD3D12AddressMode(description.AddressU);
            l_Description.AddressV = ToD3D12AddressMode(description.AddressV);
            l_Description.AddressW = ToD3D12AddressMode(description.AddressW);
            l_Description.MipLODBias = description.MipLodBias;
            l_Description.MaxAnisotropy = 1;
            l_Description.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            l_Description.MinLOD = description.MinLod;
            l_Description.MaxLOD = description.MaxLod >= c_LodUnclamped ? D3D12_FLOAT32_MAX : description.MaxLod;
            m_Device.GetHandle()->CreateSampler(&l_Description, m_SamplerHeap.GetHandle(l_Sampler.Index));

            return m_Samplers.Add(l_Sampler);
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
                D3D12Release l_Release;
                l_Release.SamplerIndex = l_Sampler->Index;
                m_Releases.Push(std::move(l_Release));
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