#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#include "Trinity/RHI/D3D12/D3D12SwapChain.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Platform.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            using Microsoft::WRL::ComPtr;

            struct AdapterSupport
            {
                D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_12_0;
                D3D_SHADER_MODEL ShaderModel = D3D_SHADER_MODEL_5_1;
                D3D12_RESOURCE_BINDING_TIER BindingTier = D3D12_RESOURCE_BINDING_TIER_1;
                bool EnhancedBarriers = false;
            };

            std::string ToUtf8(std::wstring_view text)
            {
                if (text.empty())
                {
                    return {};
                }

                const int l_Size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
                std::string l_Result(static_cast<std::size_t>(l_Size), '\0');
                ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), l_Result.data(), l_Size, nullptr, nullptr);

                return l_Result;
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

            std::string FormatFeatureLevel(D3D_FEATURE_LEVEL level)
            {
                const std::uint32_t l_Level = static_cast<std::uint32_t>(level);

                return std::format("{}_{}", l_Level >> 12, (l_Level >> 8) & 0xF);
            }

            std::string FormatShaderModel(D3D_SHADER_MODEL model)
            {
                const std::uint32_t l_Model = static_cast<std::uint32_t>(model);

                return std::format("{}.{}", l_Model >> 4, l_Model & 0xF);
            }

            std::filesystem::path GetModulePath(HMODULE module)
            {
                std::wstring l_Path(MAX_PATH, L'\0');
                while (true)
                {
                    const DWORD l_Length = ::GetModuleFileNameW(module, l_Path.data(), static_cast<DWORD>(l_Path.size()));
                    if (l_Length == 0)
                    {
                        return {};
                    }

                    if (l_Length < l_Path.size())
                    {
                        l_Path.resize(l_Length);

                        return l_Path;
                    }

                    l_Path.resize(l_Path.size() * 2);
                }
            }

            AdapterSupport QuerySupport(ID3D12Device* device)
            {
                AdapterSupport l_Support;

                const std::array<D3D_FEATURE_LEVEL, 3> l_RequestedLevels{ D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2 };
                D3D12_FEATURE_DATA_FEATURE_LEVELS l_Levels{};
                l_Levels.NumFeatureLevels = static_cast<UINT>(l_RequestedLevels.size());
                l_Levels.pFeatureLevelsRequested = l_RequestedLevels.data();
                if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &l_Levels, sizeof(l_Levels))))
                {
                    l_Support.FeatureLevel = l_Levels.MaxSupportedFeatureLevel;
                }

                // A runtime rejects shader models newer than it knows, so start at the newest these headers know and step down
                for (std::uint32_t it_Model = static_cast<std::uint32_t>(D3D_HIGHEST_SHADER_MODEL); it_Model >= static_cast<std::uint32_t>(D3D_SHADER_MODEL_6_0); --it_Model)
                {
                    D3D12_FEATURE_DATA_SHADER_MODEL l_ShaderModel{ static_cast<D3D_SHADER_MODEL>(it_Model) };
                    const HRESULT l_Result = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &l_ShaderModel, sizeof(l_ShaderModel));
                    if (SUCCEEDED(l_Result))
                    {
                        l_Support.ShaderModel = l_ShaderModel.HighestShaderModel;

                        break;
                    }

                    if (l_Result != E_INVALIDARG)
                    {
                        break;
                    }
                }

                D3D12_FEATURE_DATA_D3D12_OPTIONS l_Options{};
                if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &l_Options, sizeof(l_Options))))
                {
                    l_Support.BindingTier = l_Options.ResourceBindingTier;
                }

                D3D12_FEATURE_DATA_D3D12_OPTIONS12 l_Options12{};
                if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &l_Options12, sizeof(l_Options12))))
                {
                    l_Support.EnhancedBarriers = l_Options12.EnhancedBarriersSupported != FALSE;
                }

                return l_Support;
            }

            // Empty when the adapter has everything the engine is built on
            std::string GetMissingFeatures(const AdapterSupport& support)
            {
                std::string l_Missing;
                const auto a_Add = [&l_Missing](std::string_view feature)
                {
                    l_Missing += l_Missing.empty() ? "" : ", ";
                    l_Missing += feature;
                };

                if (support.ShaderModel < D3D_SHADER_MODEL_6_6)
                {
                    a_Add("Shader Model 6.6");
                }

                if (support.BindingTier < D3D12_RESOURCE_BINDING_TIER_3)
                {
                    a_Add("resource binding tier 3");
                }

                if (!support.EnhancedBarriers)
                {
                    a_Add("enhanced barriers");
                }

                return l_Missing;
            }

            // D3D12MemoryAllocator's bookkeeping, counted under the Renderer tag, what the driver allocates is out of reach, since D3D12 takes no allocator
            void* AllocateHostMemory(std::size_t size, std::size_t alignment, [[maybe_unused]] void* userData)
            {
                return Memory::TryAllocate(size, MemoryTag::Renderer, alignment);
            }

            void FreeHostMemory(void* memory, [[maybe_unused]] void* userData)
            {
                Memory::Free(memory);
            }

            constexpr D3D12MA::ALLOCATION_CALLBACKS c_HostAllocator{ &AllocateHostMemory, &FreeHostMemory, nullptr };

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

            struct D3D12State
            {
                D3D12_BARRIER_SYNC Sync = D3D12_BARRIER_SYNC_NONE;
                D3D12_BARRIER_ACCESS Access = D3D12_BARRIER_ACCESS_NO_ACCESS;
                D3D12_BARRIER_LAYOUT Layout = D3D12_BARRIER_LAYOUT_UNDEFINED;
            };

            D3D12State ToD3D12State(ResourceState state)
            {
                switch (state)
                {
                    case ResourceState::Undefined:
                    {
                        return { D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_BARRIER_LAYOUT_UNDEFINED };
                    }
                    case ResourceState::Present:
                    {
                        return { D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_BARRIER_LAYOUT_PRESENT };
                    }
                    case ResourceState::RenderTarget:
                    {
                        return { D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_LAYOUT_RENDER_TARGET };
                    }
                    case ResourceState::DepthWrite:
                    {
                        return { D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE };
                    }
                    case ResourceState::DepthRead:
                    {
                        return { D3D12_BARRIER_SYNC_DEPTH_STENCIL | D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ | D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ };
                    }
                    case ResourceState::ShaderResource:
                    {
                        return { D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE };
                    }
                    case ResourceState::UnorderedAccess:
                    {
                        return { D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS };
                    }
                    case ResourceState::CopySource:
                    {
                        return { D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_LAYOUT_COPY_SOURCE };
                    }
                    case ResourceState::CopyDestination:
                    {
                        return { D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_COPY_DEST };
                    }
                    case ResourceState::IndexBuffer:
                    {
                        return { D3D12_BARRIER_SYNC_INDEX_INPUT, D3D12_BARRIER_ACCESS_INDEX_BUFFER, D3D12_BARRIER_LAYOUT_UNDEFINED };
                    }
                    case ResourceState::IndirectArgument:
                    {
                        return { D3D12_BARRIER_SYNC_EXECUTE_INDIRECT, D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT, D3D12_BARRIER_LAYOUT_UNDEFINED };
                    }
                }

                return {};
            }

            D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE ToBeginningAccess(LoadOp load)
            {
                switch (load)
                {
                    case LoadOp::Load:
                    {
                        return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
                    }
                    case LoadOp::Clear:
                    {
                        return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
                    }
                    default:
                    {
                        return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
                    }
                }
            }

            D3D12_RENDER_PASS_ENDING_ACCESS_TYPE ToEndingAccess(StoreOp store)
            {
                return store == StoreOp::Store ? D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE : D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
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

        bool D3D12DescriptorHeap::Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t capacity, bool shaderVisible)
        {
            D3D12_DESCRIPTOR_HEAP_DESC l_Description{};
            l_Description.Type = type;
            l_Description.NumDescriptors = capacity;
            l_Description.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(device->CreateDescriptorHeap(&l_Description, IID_PPV_ARGS(&m_Heap))))
            {
                return false;
            }

            m_Start = m_Heap->GetCPUDescriptorHandleForHeapStart();
            m_Increment = device->GetDescriptorHandleIncrementSize(type);
            m_Indices.Reset(capacity);

            return true;
        }

        void D3D12DescriptorHeap::Reset()
        {
            m_Heap.Reset();
        }

        std::uint32_t D3D12DescriptorHeap::Allocate()
        {
            return m_Indices.Allocate();
        }

        void D3D12DescriptorHeap::Free(std::uint32_t index)
        {
            m_Indices.Free(index);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE D3D12DescriptorHeap::GetHandle(std::uint32_t index) const
        {
            return { m_Start.ptr + static_cast<SIZE_T>(index) * m_Increment };
        }

        D3D12CommandList::D3D12CommandList(D3D12Device& device) : m_Device(device)
        {

        }

        void D3D12CommandList::Begin(ID3D12GraphicsCommandList7* commandList)
        {
            m_CommandList = commandList;
            m_Rendering = false;
            m_HasPipeline = false;

            const std::array<ID3D12DescriptorHeap*, 2> l_Heaps{ m_Device.GetResourceHeap(), m_Device.GetSamplerHeap() };
            m_CommandList->SetDescriptorHeaps(static_cast<UINT>(l_Heaps.size()), l_Heaps.data());
            m_CommandList->SetGraphicsRootSignature(m_Device.GetRootSignature());
        }

        void D3D12CommandList::End()
        {
            TR_CORE_ASSERT(!m_Rendering, "The frame ended inside BeginRendering.");

            m_CommandList->Close();
            m_CommandList = nullptr;
        }

        // Moving out of Undefined discards the contents, which also counts as the first write a render target or depth texture needs
        void D3D12CommandList::TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering, "Barriers are recorded within a frame and outside rendering.");
            TR_CORE_ASSERT(after != ResourceState::Undefined, "A texture cannot move into the undefined state.");

            const D3D12Texture* l_Texture = m_Device.GetTexture(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "TextureBarrier on a destroyed or invalid texture.");
            if (l_Texture == nullptr)
            {
                return;
            }

            const D3D12State l_Before = ToD3D12State(before);
            const D3D12State l_After = ToD3D12State(after);

            D3D12_TEXTURE_BARRIER l_Barrier{};
            l_Barrier.SyncBefore = l_Before.Sync;
            l_Barrier.SyncAfter = l_After.Sync;
            l_Barrier.AccessBefore = l_Before.Access;
            l_Barrier.AccessAfter = l_After.Access;
            l_Barrier.LayoutBefore = l_Before.Layout;
            l_Barrier.LayoutAfter = l_After.Layout;
            l_Barrier.pResource = l_Texture->Resource;
            l_Barrier.Subresources.IndexOrFirstMipLevel = 0xFFFFFFFF;
            l_Barrier.Flags = before == ResourceState::Undefined ? D3D12_TEXTURE_BARRIER_FLAG_DISCARD : D3D12_TEXTURE_BARRIER_FLAG_NONE;

            D3D12_BARRIER_GROUP l_Group{};
            l_Group.Type = D3D12_BARRIER_TYPE_TEXTURE;
            l_Group.NumBarriers = 1;
            l_Group.pTextureBarriers = &l_Barrier;
            m_CommandList->Barrier(1, &l_Group);
        }

        void D3D12CommandList::BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering, "Barriers are recorded within a frame and outside rendering.");

            const D3D12Buffer* l_Buffer = m_Device.GetBuffer(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "BufferBarrier on a destroyed or invalid buffer.");
            if (l_Buffer == nullptr)
            {
                return;
            }

            const D3D12State l_Before = ToD3D12State(before);
            const D3D12State l_After = ToD3D12State(after);

            D3D12_BUFFER_BARRIER l_Barrier{};
            l_Barrier.SyncBefore = l_Before.Sync;
            l_Barrier.SyncAfter = l_After.Sync;
            l_Barrier.AccessBefore = l_Before.Access;
            l_Barrier.AccessAfter = l_After.Access;
            l_Barrier.pResource = l_Buffer->Allocation->GetResource();
            l_Barrier.Offset = 0;
            l_Barrier.Size = UINT64_MAX;

            D3D12_BARRIER_GROUP l_Group{};
            l_Group.Type = D3D12_BARRIER_TYPE_BUFFER;
            l_Group.NumBarriers = 1;
            l_Group.pBufferBarriers = &l_Barrier;
            m_CommandList->Barrier(1, &l_Group);
        }

        // A render pass clears whole attachments, so the render area only matters to Vulkan
        void D3D12CommandList::BeginRendering(const RenderingDescription& description)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering, "BeginRendering is called once within a frame, before EndRendering.");
            TR_CORE_ASSERT(description.ColorAttachments.size() <= c_MaxColorAttachments, "Too many color attachments.");

            std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, c_MaxColorAttachments> l_Targets{};
            UINT l_TargetCount = 0;
            for (const ColorAttachment& it_Attachment : description.ColorAttachments)
            {
                const D3D12Texture* l_Texture = m_Device.GetTexture(it_Attachment.Texture);
                TR_CORE_ASSERT(l_Texture != nullptr && l_Texture->RenderTargetView != c_NoDescriptor, "BeginRendering with a destroyed texture, or one without RenderTarget usage.");
                if (l_Texture == nullptr || l_Texture->RenderTargetView == c_NoDescriptor || l_TargetCount == c_MaxColorAttachments)
                {
                    continue;
                }

                D3D12_RENDER_PASS_RENDER_TARGET_DESC& l_Target = l_Targets[l_TargetCount++];
                l_Target.cpuDescriptor = m_Device.GetRenderTargetView(*l_Texture);
                l_Target.BeginningAccess.Type = ToBeginningAccess(it_Attachment.Load);
                l_Target.BeginningAccess.Clear.ClearValue.Format = ToDXGIFormat(l_Texture->TextureFormat);
                for (std::size_t it_Channel = 0; it_Channel < it_Attachment.ClearColor.size(); ++it_Channel)
                {
                    l_Target.BeginningAccess.Clear.ClearValue.Color[it_Channel] = it_Attachment.ClearColor[it_Channel];
                }

                l_Target.EndingAccess.Type = ToEndingAccess(it_Attachment.Store);
            }

            D3D12_RENDER_PASS_DEPTH_STENCIL_DESC l_Depth{};
            const D3D12Texture* l_DepthTexture = description.Depth.Texture ? m_Device.GetTexture(description.Depth.Texture) : nullptr;
            TR_CORE_ASSERT(!description.Depth.Texture || (l_DepthTexture != nullptr && l_DepthTexture->DepthStencilView != c_NoDescriptor), "BeginRendering with a destroyed depth texture, or one without DepthStencil usage.");
            const bool l_HasDepth = l_DepthTexture != nullptr && l_DepthTexture->DepthStencilView != c_NoDescriptor;
            if (l_HasDepth)
            {
                l_Depth.cpuDescriptor = m_Device.GetDepthStencilView(*l_DepthTexture);
                l_Depth.DepthBeginningAccess.Type = ToBeginningAccess(description.Depth.Load);
                l_Depth.DepthBeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_D32_FLOAT;
                l_Depth.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = description.Depth.ClearDepth;
                l_Depth.DepthEndingAccess.Type = ToEndingAccess(description.Depth.Store);
                l_Depth.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
                l_Depth.StencilEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
            }

            m_CommandList->BeginRenderPass(l_TargetCount, l_Targets.data(), l_HasDepth ? &l_Depth : nullptr, D3D12_RENDER_PASS_FLAG_NONE);
            m_Rendering = true;
        }

        void D3D12CommandList::EndRendering()
        {
            TR_CORE_ASSERT(m_Rendering, "EndRendering without BeginRendering.");

            m_CommandList->EndRenderPass();
            m_Rendering = false;
            m_HasPipeline = false;
        }

        void D3D12CommandList::SetPipeline(PipelineHandle pipeline)
        {
            TR_CORE_ASSERT(m_Rendering, "SetPipeline is recorded inside rendering.");

            const D3D12Pipeline* l_Pipeline = m_Device.GetPipeline(pipeline);
            TR_CORE_ASSERT(l_Pipeline != nullptr, "SetPipeline with a destroyed or invalid pipeline.");
            if (l_Pipeline == nullptr)
            {
                return;
            }

            m_CommandList->SetPipelineState(l_Pipeline->State.Get());
            m_CommandList->IASetPrimitiveTopology(l_Pipeline->Topology);
            m_HasPipeline = true;
        }

        void D3D12CommandList::SetViewport(const Viewport& viewport)
        {
            TR_CORE_ASSERT(m_Rendering, "SetViewport is recorded inside rendering.");

            const D3D12_VIEWPORT l_Viewport{ viewport.X, viewport.Y, viewport.Width, viewport.Height, viewport.MinDepth, viewport.MaxDepth };
            m_CommandList->RSSetViewports(1, &l_Viewport);
        }

        void D3D12CommandList::SetScissor(const Rect& scissor)
        {
            TR_CORE_ASSERT(m_Rendering, "SetScissor is recorded inside rendering.");

            const D3D12_RECT l_Scissor{ static_cast<LONG>(scissor.X), static_cast<LONG>(scissor.Y), static_cast<LONG>(scissor.X) + static_cast<LONG>(scissor.Width), static_cast<LONG>(scissor.Y) + static_cast<LONG>(scissor.Height) };
            m_CommandList->RSSetScissorRects(1, &l_Scissor);
        }

        // The push constants are root constants at b0, the first parameter of the shared root signature
        void D3D12CommandList::PushConstants(std::span<const std::byte> data)
        {
            TR_CORE_ASSERT(m_HasPipeline, "PushConstants needs a pipeline.");
            TR_CORE_ASSERT(data.size() <= c_MaxPushConstantSize && data.size() % 4 == 0, "Push constants are whole 32-bit values, at most c_MaxPushConstantSize bytes.");

            m_CommandList->SetGraphicsRoot32BitConstants(0, static_cast<UINT>(data.size() / 4), data.data(), 0);
        }

        void D3D12CommandList::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance)
        {
            TR_CORE_ASSERT(m_HasPipeline, "Draw needs a pipeline.");

            m_CommandList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
        }

        void D3D12CommandList::CopyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination, std::uint64_t destinationOffset, std::uint64_t size)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering, "Copies are recorded within a frame and outside rendering.");

            const D3D12Buffer* l_Source = m_Device.GetBuffer(source);
            const D3D12Buffer* l_Destination = m_Device.GetBuffer(destination);
            TR_CORE_ASSERT(l_Source != nullptr && l_Destination != nullptr, "CopyBuffer with a destroyed or invalid buffer.");
            if (l_Source == nullptr || l_Destination == nullptr)
            {
                return;
            }

            TR_CORE_ASSERT(sourceOffset + size <= l_Source->Size && destinationOffset + size <= l_Destination->Size, "CopyBuffer reaches past the end of a buffer.");

            m_CommandList->CopyBufferRegion(l_Destination->Allocation->GetResource(), destinationOffset, l_Source->Allocation->GetResource(), sourceOffset, size);
        }

        void D3D12CommandList::CopyTextureToBuffer(TextureHandle source, BufferHandle destination)
        {
            TR_CORE_ASSERT(m_CommandList != nullptr && !m_Rendering, "Copies are recorded within a frame and outside rendering.");

            const D3D12Texture* l_Texture = m_Device.GetTexture(source);
            const D3D12Buffer* l_Buffer = m_Device.GetBuffer(destination);
            TR_CORE_ASSERT(l_Texture != nullptr && l_Buffer != nullptr, "CopyTextureToBuffer with a destroyed or invalid resource.");
            if (l_Texture == nullptr || l_Buffer == nullptr)
            {
                return;
            }

            const std::uint64_t l_RowPitch = GetTextureCopyRowPitch(l_Texture->TextureFormat, l_Texture->Width);
            TR_CORE_ASSERT(l_RowPitch * l_Texture->Height <= l_Buffer->Size, "CopyTextureToBuffer needs {} bytes, and the buffer has {}.", l_RowPitch * l_Texture->Height, l_Buffer->Size);

            D3D12_TEXTURE_COPY_LOCATION l_Source{};
            l_Source.pResource = l_Texture->Resource;
            l_Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            l_Source.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION l_Destination{};
            l_Destination.pResource = l_Buffer->Allocation->GetResource();
            l_Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            l_Destination.PlacedFootprint.Offset = 0;
            l_Destination.PlacedFootprint.Footprint.Format = l_Texture->ResourceFormat;
            l_Destination.PlacedFootprint.Footprint.Width = l_Texture->Width;
            l_Destination.PlacedFootprint.Footprint.Height = l_Texture->Height;
            l_Destination.PlacedFootprint.Footprint.Depth = 1;
            l_Destination.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(l_RowPitch);

            m_CommandList->CopyTextureRegion(&l_Destination, 0, 0, 0, &l_Source, nullptr);
        }

        Scope<D3D12Device> D3D12Device::Create(const DeviceSpecification& specification, std::string& error)
        {
            Scope<D3D12Device> l_Device = CreateScope<D3D12Device>();
            if (!l_Device->Initialize(specification, error))
            {
                return nullptr;
            }

            return l_Device;
        }

        D3D12Device::D3D12Device() : m_CommandList(*this)
        {

        }

        D3D12Device::~D3D12Device()
        {
            if (m_Fence)
            {
                WaitForFence(m_FrameNumber);
            }

            if (m_Buffers.GetCount() != 0 || m_Textures.GetCount() != 0 || m_Pipelines.GetCount() != 0)
            {
                TR_CORE_WARN("D3D12: the device was destroyed with {} buffer(s), {} texture(s) and {} pipeline(s) still alive", m_Buffers.GetCount(), m_Textures.GetCount(), m_Pipelines.GetCount());
            }

            m_Releases.ReleaseAll([this](D3D12Release& release) { Release(release); });
            m_Buffers.ForEach([](D3D12Buffer& buffer) { buffer.Allocation.Reset(); });
            m_Textures.ForEach([](D3D12Texture& texture) { texture.Allocation.Reset(); });
            m_Pipelines.ForEach([](D3D12Pipeline& pipeline) { pipeline.State.Reset(); });
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

            if (m_InfoQueue && m_MessageCallbackCookie != 0)
            {
                m_InfoQueue->UnregisterMessageCallback(m_MessageCallbackCookie);
            }

            const bool l_Report = m_Validation && m_Device;

            m_InfoQueue.Reset();
            m_Device.Reset();
            m_Adapter.Reset();
            m_Factory.Reset();

            if (l_Report)
            {
                TR_CORE_INFO("D3D12 debug layer: {} warning(s) or error(s) while the device lived", m_MessageCount.load());
                ReportLiveObjects();
            }
        }

        bool D3D12Device::Initialize(const DeviceSpecification& specification, std::string& error)
        {
            m_Validation = specification.EnableValidation;

            // The debug layer must be on before the device exists
            UINT l_FactoryFlags = 0;
            if (m_Validation)
            {
                ComPtr<ID3D12Debug1> l_Debug;
                if (SUCCEEDED(::D3D12GetDebugInterface(IID_PPV_ARGS(&l_Debug))))
                {
                    l_Debug->EnableDebugLayer();
                    l_Debug->SetEnableGPUBasedValidation(specification.EnableGPUValidation ? TRUE : FALSE);
                    l_FactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

                    TR_CORE_INFO("D3D12: debug layer on{}", specification.EnableGPUValidation ? ", with GPU-based validation" : "");
                }
                else
                {
                    TR_CORE_WARN("D3D12: the debug layer is missing, so validation is off. Add the Graphics Tools optional feature in Windows settings");
                    m_Validation = false;
                }
            }

            HRESULT l_Result = ::CreateDXGIFactory2(l_FactoryFlags, IID_PPV_ARGS(&m_Factory));
            if (FAILED(l_Result))
            {
                error = std::format("CreateDXGIFactory2 failed with {}", FormatResult(l_Result));

                return false;
            }

            std::string l_Skipped;
            ComPtr<IDXGIAdapter1> l_Adapter;
            for (UINT it_Index = 0; m_Factory->EnumAdapterByGpuPreference(it_Index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&l_Adapter)) != DXGI_ERROR_NOT_FOUND; ++it_Index)
            {
                DXGI_ADAPTER_DESC1 l_Description{};
                l_Adapter->GetDesc1(&l_Description);
                if ((l_Description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                {
                    continue;
                }

                const std::string l_Name = ToUtf8(l_Description.Description);

                ComPtr<ID3D12Device10> l_Device;
                l_Result = ::D3D12CreateDevice(l_Adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&l_Device));
                if (FAILED(l_Result))
                {
                    l_Skipped += std::format("{}{}: no feature level 12_0 device ({})", l_Skipped.empty() ? "" : "; ", l_Name, FormatResult(l_Result));

                    continue;
                }

                const AdapterSupport l_Support = QuerySupport(l_Device.Get());
                const std::string l_Missing = GetMissingFeatures(l_Support);
                if (!l_Missing.empty())
                {
                    l_Skipped += std::format("{}{}: no {}", l_Skipped.empty() ? "" : "; ", l_Name, l_Missing);

                    continue;
                }

                m_Adapter = l_Adapter;
                m_Device = l_Device;

                m_Info.API = GraphicsAPI::D3D12;
                m_Info.AdapterName = l_Name;
                m_Info.VideoMemoryBytes = l_Description.DedicatedVideoMemory;

                TR_CORE_INFO("D3D12: {} with {} of video memory: feature level {}, Shader Model {}, resource binding tier {}, enhanced barriers", l_Name, Memory::FormatBytes(m_Info.VideoMemoryBytes), FormatFeatureLevel(l_Support.FeatureLevel), FormatShaderModel(l_Support.ShaderModel), static_cast<int>(l_Support.BindingTier));

                break;
            }

            if (!m_Device)
            {
                error = l_Skipped.empty() ? std::string("no hardware adapter was found") : std::format("no adapter has what the engine needs ({})", l_Skipped);

                return false;
            }

            EnableDebugMessages();
            LogRuntime();

            return CreateAllocator(error) && CreateFrames(error) && CreateBindless(error);
        }

        bool D3D12Device::CreateAllocator(std::string& error)
        {
            D3D12MA::ALLOCATOR_DESC l_Description{};
            // D3D12MemoryAllocator's flag operators are global, and RHI's own operator| hides them in here
            l_Description.Flags = static_cast<D3D12MA::ALLOCATOR_FLAGS>(D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS);
            l_Description.pDevice = m_Device.Get();
            l_Description.pAdapter = m_Adapter.Get();
            l_Description.pAllocationCallbacks = &c_HostAllocator;

            const HRESULT l_Result = D3D12MA::CreateAllocator(&l_Description, &m_Allocator);
            if (FAILED(l_Result))
            {
                error = std::format("D3D12MemoryAllocator could not be created on {} ({})", m_Info.AdapterName, FormatResult(l_Result));

                return false;
            }

            return true;
        }

        bool D3D12Device::CreateFrames(std::string& error)
        {
            D3D12_COMMAND_QUEUE_DESC l_QueueDescription{};
            l_QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

            HRESULT l_Result = m_Device->CreateCommandQueue(&l_QueueDescription, IID_PPV_ARGS(&m_Queue));
            for (std::size_t it_Index = 0; SUCCEEDED(l_Result) && it_Index < m_Frames.size(); ++it_Index)
            {
                l_Result = m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_Frames[it_Index].Allocator));
            }

            if (SUCCEEDED(l_Result))
            {
                l_Result = m_Device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_GraphicsList));
            }

            if (SUCCEEDED(l_Result))
            {
                l_Result = m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
            }

            if (FAILED(l_Result))
            {
                error = std::format("the command queue, command lists and fence could not be created on {} ({})", m_Info.AdapterName, FormatResult(l_Result));

                return false;
            }

            m_FenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (m_FenceEvent == nullptr || !m_RenderTargetViews.Initialize(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 256, false) || !m_DepthStencilViews.Initialize(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64, false))
            {
                error = std::format("the fence event and view heaps could not be created on {}", m_Info.AdapterName);

                return false;
            }

            m_Queue->SetName(L"Trinity direct queue");
            m_GraphicsList->SetName(L"Trinity frame commands");

            BOOL l_Tearing = FALSE;
            m_TearingSupported = SUCCEEDED(m_Factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &l_Tearing, sizeof(l_Tearing))) && l_Tearing == TRUE;

            return true;
        }

        // Shaders index the two heaps directly, so every pipeline shares one root signature that holds only the push constants
        bool D3D12Device::CreateBindless(std::string& error)
        {
            if (!m_ResourceHeap.Initialize(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, c_BindlessResourceCapacity, true) || !m_SamplerHeap.Initialize(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, c_BindlessSamplerCapacity, true))
            {
                error = std::format("the shader-visible descriptor heaps could not be created on {}", m_Info.AdapterName);

                return false;
            }

            D3D12_ROOT_PARAMETER1 l_PushConstants{};
            l_PushConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            l_PushConstants.Constants.ShaderRegister = 0;
            l_PushConstants.Constants.RegisterSpace = 0;
            l_PushConstants.Constants.Num32BitValues = c_MaxPushConstantSize / 4;
            l_PushConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC l_Description{};
            l_Description.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
            l_Description.Desc_1_1.NumParameters = 1;
            l_Description.Desc_1_1.pParameters = &l_PushConstants;
            l_Description.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

            ComPtr<ID3DBlob> l_Blob;
            ComPtr<ID3DBlob> l_Errors;
            HRESULT l_Result = ::D3D12SerializeVersionedRootSignature(&l_Description, &l_Blob, &l_Errors);
            if (SUCCEEDED(l_Result))
            {
                l_Result = m_Device->CreateRootSignature(0, l_Blob->GetBufferPointer(), l_Blob->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature));
            }

            if (FAILED(l_Result))
            {
                const std::string_view l_Reason = l_Errors ? std::string_view(static_cast<const char*>(l_Errors->GetBufferPointer()), l_Errors->GetBufferSize()) : std::string_view();
                error = std::format("the bindless root signature could not be created on {} ({}{}{})", m_Info.AdapterName, FormatResult(l_Result), l_Reason.empty() ? "" : ": ", l_Reason);

                return false;
            }

            m_ResourceHeap.GetHeap()->SetName(L"Trinity bindless resources");
            m_SamplerHeap.GetHeap()->SetName(L"Trinity bindless samplers");
            m_RootSignature->SetName(L"Trinity bindless root signature");

            return true;
        }

        void D3D12Device::EnableDebugMessages()
        {
            if (!m_Validation)
            {
                return;
            }

            if (FAILED(m_Device.As(&m_InfoQueue)))
            {
                TR_CORE_WARN("D3D12: this runtime has no ID3D12InfoQueue1, so debug layer messages only reach an attached debugger");

                return;
            }

            if (FAILED(m_InfoQueue->RegisterMessageCallback(&D3D12Device::OnDebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &m_MessageCallbackCookie)))
            {
                TR_CORE_WARN("D3D12: the debug layer message callback could not be registered, so messages only reach an attached debugger");
                m_InfoQueue.Reset();
            }
        }

        // The debug layer calls this from whichever thread made the call it reports on
        void CALLBACK D3D12Device::OnDebugMessage([[maybe_unused]] D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context)
        {
            D3D12Device* l_Device = static_cast<D3D12Device*>(context);

            switch (severity)
            {
                case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                case D3D12_MESSAGE_SEVERITY_ERROR:
                {
                    l_Device->m_MessageCount.fetch_add(1, std::memory_order_relaxed);
                    TR_CORE_ERROR("D3D12 debug layer: {} (message {})", description, static_cast<int>(id));

                    break;
                }
                case D3D12_MESSAGE_SEVERITY_WARNING:
                {
                    l_Device->m_MessageCount.fetch_add(1, std::memory_order_relaxed);
                    TR_CORE_WARN("D3D12 debug layer: {} (message {})", description, static_cast<int>(id));

                    break;
                }
                case D3D12_MESSAGE_SEVERITY_INFO:
                case D3D12_MESSAGE_SEVERITY_MESSAGE:
                {
                    TR_CORE_TRACE("D3D12 debug layer: {}", description);

                    break;
                }
            }
        }

        // Says which D3D12Core.dll the process loaded: the Agility SDK's beside the executable, or the one built into Windows
        void D3D12Device::LogRuntime() const
        {
            const std::filesystem::path l_Path = GetModulePath(::GetModuleHandleW(L"D3D12Core.dll"));

            std::error_code l_Error;
            [[maybe_unused]] const bool l_Agility = !l_Path.empty() && std::filesystem::equivalent(l_Path.parent_path(), Platform::GetExecutableDirectory() / "D3D12", l_Error);

#if defined(TR_AGILITY_SDK_VERSION)
            if (l_Agility)
            {
                TR_CORE_INFO("D3D12: Agility SDK {} runtime loaded from '{}'", TR_AGILITY_SDK_VERSION, l_Path.string());
            }
            else
            {
                TR_CORE_WARN("D3D12: the Agility SDK {} runtime beside the executable was not loaded, so the Windows runtime at '{}' is in use", TR_AGILITY_SDK_VERSION, l_Path.string());
            }
#else
            TR_CORE_INFO("D3D12: this build has no Agility SDK, so the Windows runtime at '{}' is in use", l_Path.string());
#endif
        }

        // Runs after every D3D12 and DXGI object of the device is released, so anything still reported is a leak
        void D3D12Device::ReportLiveObjects() const
        {
            ComPtr<IDXGIDebug1> l_Debug;
            ComPtr<IDXGIInfoQueue> l_Queue;
            if (FAILED(::DXGIGetDebugInterface1(0, IID_PPV_ARGS(&l_Debug))) || FAILED(::DXGIGetDebugInterface1(0, IID_PPV_ARGS(&l_Queue))))
            {
                return;
            }

            l_Debug->ReportLiveObjects(DXGI_DEBUG_ALL, static_cast<DXGI_DEBUG_RLO_FLAGS>(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));

            const UINT64 l_Count = l_Queue->GetNumStoredMessages(DXGI_DEBUG_ALL);
            for (UINT64 it_Index = 0; it_Index < l_Count; ++it_Index)
            {
                SIZE_T l_Size = 0;
                l_Queue->GetMessage(DXGI_DEBUG_ALL, it_Index, nullptr, &l_Size);

                std::vector<std::byte> l_Storage(l_Size);
                DXGI_INFO_QUEUE_MESSAGE* l_Message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(l_Storage.data());
                if (SUCCEEDED(l_Queue->GetMessage(DXGI_DEBUG_ALL, it_Index, l_Message, &l_Size)))
                {
                    TR_CORE_WARN("DXGI debug: {}", std::string_view(l_Message->pDescription, l_Message->DescriptionByteLength > 0 ? l_Message->DescriptionByteLength - 1 : 0));
                }
            }

            l_Queue->ClearStoredMessages(DXGI_DEBUG_ALL);

            if (l_Count == 0)
            {
                TR_CORE_INFO("D3D12: no live objects after the device was destroyed");
            }
        }

        // Buffers always start in the undefined layout under enhanced barriers. Upload and Readback heaps are coherent and stay mapped
        BufferHandle D3D12Device::CreateBuffer(const BufferDescription& description)
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
            HRESULT l_Result = m_Allocator->CreateResource3(&l_Allocation, &l_Resource, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, 0, nullptr, &l_Buffer.Allocation, IID_NULL, nullptr);
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

        void D3D12Device::DestroyBuffer(BufferHandle buffer)
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

        std::span<std::byte> D3D12Device::GetMappedData(BufferHandle buffer)
        {
            const D3D12Buffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetMappedData on a destroyed or invalid buffer.");

            if (l_Buffer == nullptr || l_Buffer->Mapped == nullptr)
            {
                return {};
            }

            return { l_Buffer->Mapped, static_cast<std::size_t>(l_Buffer->Size) };
        }

        // A depth texture that shaders also read is typeless, so a shader view can read it as R32_FLOAT
        TextureHandle D3D12Device::CreateTexture(const TextureDescription& description)
        {
            TR_CORE_ASSERT(description.Width != 0 && description.Height != 0 && description.MipLevels != 0, "Texture '{}' has a zero size or no mips.", description.DebugName);
            TR_CORE_ASSERT(description.TextureFormat != Format::Unknown, "Texture '{}' has no format.", description.DebugName);

            const bool l_TypelessDepth = description.TextureFormat == Format::D32Float && HasFlag(description.Usage, TextureUsage::ShaderResource);

            D3D12MA::ALLOCATION_DESC l_Allocation{};
            l_Allocation.HeapType = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC1 l_Resource{};
            l_Resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            l_Resource.Width = description.Width;
            l_Resource.Height = description.Height;
            l_Resource.DepthOrArraySize = 1;
            l_Resource.MipLevels = static_cast<UINT16>(description.MipLevels);
            l_Resource.Format = l_TypelessDepth ? DXGI_FORMAT_R32_TYPELESS : ToDXGIFormat(description.TextureFormat);
            l_Resource.SampleDesc.Count = 1;
            l_Resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            l_Resource.Flags = ToResourceFlags(description.Usage);

            D3D12_CLEAR_VALUE l_ClearValue{};
            const D3D12_CLEAR_VALUE* l_OptimizedClear = nullptr;
            if (HasFlag(description.Usage, TextureUsage::RenderTarget))
            {
                l_ClearValue.Format = ToDXGIFormat(description.TextureFormat);
                std::ranges::copy(description.ClearColor, l_ClearValue.Color);
                l_OptimizedClear = &l_ClearValue;
            }
            else if (HasFlag(description.Usage, TextureUsage::DepthStencil))
            {
                l_ClearValue.Format = DXGI_FORMAT_D32_FLOAT;
                l_ClearValue.DepthStencil.Depth = description.ClearDepth;
                l_OptimizedClear = &l_ClearValue;
            }

            D3D12Texture l_Texture;
            const HRESULT l_Result = m_Allocator->CreateResource3(&l_Allocation, &l_Resource, D3D12_BARRIER_LAYOUT_COMMON, l_OptimizedClear, 0, nullptr, &l_Texture.Allocation, IID_NULL, nullptr);
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: texture '{}' ({}x{} {}) could not be created ({})", description.DebugName, description.Width, description.Height, ToString(description.TextureFormat), FormatResult(l_Result));

                return {};
            }

            l_Texture.Resource = l_Texture.Allocation->GetResource();
            l_Texture.TextureFormat = description.TextureFormat;
            l_Texture.ResourceFormat = l_Resource.Format;
            l_Texture.Width = description.Width;
            l_Texture.Height = description.Height;
            l_Texture.MipLevels = description.MipLevels;
            SetDebugName(l_Texture.Resource, description.DebugName);

            if (HasFlag(description.Usage, TextureUsage::RenderTarget) || HasFlag(description.Usage, TextureUsage::DepthStencil))
            {
                CreateViews(l_Texture);
            }

            CreateShaderViews(l_Texture, description.Usage);

            return m_Textures.Add(std::move(l_Texture));
        }

        void D3D12Device::DestroyTexture(TextureHandle texture)
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
                l_Release.RenderTargetView = l_Texture->RenderTargetView;
                l_Release.DepthStencilView = l_Texture->DepthStencilView;
                l_Release.ShaderResourceIndex = l_Texture->ShaderResourceIndex;
                l_Release.UnorderedAccessIndex = l_Texture->UnorderedAccessIndex;
                m_Releases.Push(std::move(l_Release));
            }
        }

        void D3D12Device::CreateViews(D3D12Texture& texture)
        {
            if (IsDepthFormat(texture.TextureFormat))
            {
                texture.DepthStencilView = m_DepthStencilViews.Allocate();
                TR_CORE_ASSERT(texture.DepthStencilView != c_NoDescriptor, "Out of depth stencil views.");
                if (texture.DepthStencilView != c_NoDescriptor)
                {
                    D3D12_DEPTH_STENCIL_VIEW_DESC l_View{};
                    l_View.Format = DXGI_FORMAT_D32_FLOAT;
                    l_View.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                    m_Device->CreateDepthStencilView(texture.Resource, &l_View, m_DepthStencilViews.GetHandle(texture.DepthStencilView));
                }

                return;
            }

            texture.RenderTargetView = m_RenderTargetViews.Allocate();
            TR_CORE_ASSERT(texture.RenderTargetView != c_NoDescriptor, "Out of render target views.");
            if (texture.RenderTargetView != c_NoDescriptor)
            {
                D3D12_RENDER_TARGET_VIEW_DESC l_View{};
                l_View.Format = ToDXGIFormat(texture.TextureFormat);
                l_View.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                m_Device->CreateRenderTargetView(texture.Resource, &l_View, m_RenderTargetViews.GetHandle(texture.RenderTargetView));
            }
        }

        // Buffers get raw views, which is what a ByteAddressBuffer reads and writes
        void D3D12Device::CreateShaderViews(D3D12Buffer& buffer, BufferUsage usage)
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
                    m_Device->CreateShaderResourceView(l_Resource, &l_View, m_ResourceHeap.GetHandle(buffer.ShaderResourceIndex));
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
                    m_Device->CreateUnorderedAccessView(l_Resource, nullptr, &l_View, m_ResourceHeap.GetHandle(buffer.UnorderedAccessIndex));
                }
            }
        }

        // The shader resource view covers every mip, and the unordered access view mip 0
        void D3D12Device::CreateShaderViews(D3D12Texture& texture, TextureUsage usage)
        {
            if (HasFlag(usage, TextureUsage::ShaderResource))
            {
                texture.ShaderResourceIndex = m_ResourceHeap.Allocate();
                TR_CORE_ASSERT(texture.ShaderResourceIndex != c_NoDescriptor, "Out of bindless resource indices.");
                if (texture.ShaderResourceIndex != c_NoDescriptor)
                {
                    D3D12_SHADER_RESOURCE_VIEW_DESC l_View{};
                    l_View.Format = texture.ResourceFormat == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_R32_FLOAT : texture.ResourceFormat;
                    l_View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    l_View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    l_View.Texture2D.MipLevels = texture.MipLevels;
                    m_Device->CreateShaderResourceView(texture.Resource, &l_View, m_ResourceHeap.GetHandle(texture.ShaderResourceIndex));
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
                    l_View.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    m_Device->CreateUnorderedAccessView(texture.Resource, nullptr, &l_View, m_ResourceHeap.GetHandle(texture.UnorderedAccessIndex));
                }
            }
        }

        // An index is only handed out again once the frames that could read it have finished
        void D3D12Device::Release(D3D12Release& release)
        {
            m_RenderTargetViews.Free(release.RenderTargetView);
            m_DepthStencilViews.Free(release.DepthStencilView);
            m_ResourceHeap.Free(release.ShaderResourceIndex);
            m_ResourceHeap.Free(release.UnorderedAccessIndex);
            release.Pipeline.Reset();
            release.Allocation.Reset();
        }

        TextureHandle D3D12Device::AddSwapChainBuffer(ID3D12Resource* resource, Format format, DXGI_FORMAT resourceFormat, std::uint32_t width, std::uint32_t height)
        {
            D3D12Texture l_Texture;
            l_Texture.Resource = resource;
            l_Texture.TextureFormat = format;
            l_Texture.ResourceFormat = resourceFormat;
            l_Texture.Width = width;
            l_Texture.Height = height;
            l_Texture.MipLevels = 1;
            CreateViews(l_Texture);

            return m_Textures.Add(std::move(l_Texture));
        }

        // The swap chain waits for the device to be idle first, so the view is free at once
        void D3D12Device::RemoveSwapChainBuffer(TextureHandle texture)
        {
            const std::optional<D3D12Texture> l_Texture = m_Textures.Remove(texture);
            if (l_Texture)
            {
                m_RenderTargetViews.Free(l_Texture->RenderTargetView);
            }
        }

        void D3D12Device::WaitForFence(std::uint64_t value)
        {
            if (m_Fence->GetCompletedValue() < value && SUCCEEDED(m_Fence->SetEventOnCompletion(value, m_FenceEvent)))
            {
                ::WaitForSingleObject(m_FenceEvent, INFINITE);
            }
        }

        std::uint32_t D3D12Device::GetShaderResourceIndex(BufferHandle buffer)
        {
            const D3D12Buffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetShaderResourceIndex on a destroyed or invalid buffer.");

            return l_Buffer != nullptr ? l_Buffer->ShaderResourceIndex : c_NoBindlessIndex;
        }

        std::uint32_t D3D12Device::GetUnorderedAccessIndex(BufferHandle buffer)
        {
            const D3D12Buffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetUnorderedAccessIndex on a destroyed or invalid buffer.");

            return l_Buffer != nullptr ? l_Buffer->UnorderedAccessIndex : c_NoBindlessIndex;
        }

        std::uint32_t D3D12Device::GetShaderResourceIndex(TextureHandle texture)
        {
            const D3D12Texture* l_Texture = m_Textures.Get(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "GetShaderResourceIndex on a destroyed or invalid texture.");

            return l_Texture != nullptr ? l_Texture->ShaderResourceIndex : c_NoBindlessIndex;
        }

        std::uint32_t D3D12Device::GetUnorderedAccessIndex(TextureHandle texture)
        {
            const D3D12Texture* l_Texture = m_Textures.Get(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "GetUnorderedAccessIndex on a destroyed or invalid texture.");

            return l_Texture != nullptr ? l_Texture->UnorderedAccessIndex : c_NoBindlessIndex;
        }

        PipelineHandle D3D12Device::CreateGraphicsPipeline(const GraphicsPipelineDescription& description)
        {
            TR_CORE_ASSERT(!description.VertexShader.Code.empty() && !description.PixelShader.Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);
            TR_CORE_ASSERT(description.ColorFormats.size() <= c_MaxColorAttachments, "Pipeline '{}' has too many color formats.", description.DebugName);

            D3D12_GRAPHICS_PIPELINE_STATE_DESC l_Description{};
            l_Description.pRootSignature = m_RootSignature.Get();
            l_Description.VS = { description.VertexShader.Code.data(), description.VertexShader.Code.size() };
            l_Description.PS = { description.PixelShader.Code.data(), description.PixelShader.Code.size() };
            l_Description.SampleMask = UINT_MAX;
            l_Description.PrimitiveTopologyType = ToTopologyType(description.Topology);
            l_Description.SampleDesc.Count = 1;

            l_Description.NumRenderTargets = static_cast<UINT>(std::min<std::size_t>(description.ColorFormats.size(), c_MaxColorAttachments));
            for (UINT it_Target = 0; it_Target < l_Description.NumRenderTargets; ++it_Target)
            {
                D3D12_RENDER_TARGET_BLEND_DESC& l_Blend = l_Description.BlendState.RenderTarget[it_Target];
                l_Blend.BlendEnable = description.AlphaBlend ? TRUE : FALSE;
                l_Blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                l_Blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                l_Blend.BlendOp = D3D12_BLEND_OP_ADD;
                l_Blend.SrcBlendAlpha = D3D12_BLEND_ONE;
                l_Blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                l_Blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                l_Blend.LogicOp = D3D12_LOGIC_OP_NOOP;
                l_Blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

                l_Description.RTVFormats[it_Target] = ToDXGIFormat(description.ColorFormats[it_Target]);
            }

            l_Description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            l_Description.RasterizerState.CullMode = ToCullMode(description.Cull);
            l_Description.RasterizerState.FrontCounterClockwise = description.FrontCounterClockwise ? TRUE : FALSE;
            l_Description.RasterizerState.DepthClipEnable = TRUE;

            // Vulkan never writes depth without the depth test, so neither does this
            l_Description.DepthStencilState.DepthEnable = description.DepthTest ? TRUE : FALSE;
            l_Description.DepthStencilState.DepthWriteMask = description.DepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            l_Description.DepthStencilState.DepthFunc = ToComparisonFunc(description.DepthCompare);
            l_Description.DSVFormat = description.DepthFormat != Format::Unknown ? ToDXGIFormat(description.DepthFormat) : DXGI_FORMAT_UNKNOWN;

            D3D12Pipeline l_Pipeline;
            l_Pipeline.Topology = ToD3DTopology(description.Topology);
            const HRESULT l_Result = m_Device->CreateGraphicsPipelineState(&l_Description, IID_PPV_ARGS(&l_Pipeline.State));
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

        void D3D12Device::DestroyPipeline(PipelineHandle pipeline)
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

        Scope<SwapChain> D3D12Device::CreateSwapChain(const SwapChainSpecification& specification)
        {
            return D3D12SwapChain::Create(*this, specification);
        }

        // Waits until the frame c_FramesInFlight before this one, which used the same command allocator, has finished on the GPU
        CommandList& D3D12Device::BeginFrame()
        {
            TR_CORE_ASSERT(!m_InFrame, "BeginFrame was called twice without EndFrame.");

            FrameContext& l_Frame = m_Frames[m_FrameNumber % c_FramesInFlight];
            WaitForFence(l_Frame.CompletionValue);

            m_InFrame = true;
            m_Releases.BeginFrame([this](D3D12Release& release) { Release(release); });

            l_Frame.Allocator->Reset();
            m_GraphicsList->Reset(l_Frame.Allocator.Get(), nullptr);
            m_CommandList.Begin(m_GraphicsList.Get());

            return m_CommandList;
        }

        void D3D12Device::EndFrame()
        {
            TR_CORE_ASSERT(m_InFrame, "EndFrame without BeginFrame.");

            FrameContext& l_Frame = m_Frames[m_FrameNumber % c_FramesInFlight];
            m_CommandList.End();

            ID3D12CommandList* const l_Lists[] = { m_GraphicsList.Get() };
            m_Queue->ExecuteCommandLists(1, l_Lists);

            l_Frame.CompletionValue = m_FrameNumber + 1;
            m_Queue->Signal(m_Fence.Get(), l_Frame.CompletionValue);

            ++m_FrameNumber;
            m_Releases.EndFrame();
            m_InFrame = false;
        }

        void D3D12Device::WaitIdle()
        {
            WaitForFence(m_FrameNumber);
            m_Releases.ReleaseIdle([this](D3D12Release& release) { Release(release); });
        }
    }
}