#include "Trinity/RHI/D3D12/D3D12Context.hpp"
#include "Trinity/RHI/D3D12/D3D12SwapChain.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <format>
#include <optional>
#include <string>
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

        D3D12Context::D3D12Context() : m_Device(m_Factory), m_Allocator(m_Factory, m_Device), m_RootSignature(m_Device), m_Releases(m_RenderTargetViews, m_DepthStencilViews, m_ResourceHeap, m_SamplerHeap), m_Queue(m_Device), m_CommandList(*this)
        {

        }

        D3D12Context::~D3D12Context()
        {
            if (m_Queue.IsReady())
            {
                m_Queue.WaitIdle();
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
            m_CommandList.Reset();

            for (D3D12CommandAllocator& it_Allocator : m_CommandAllocators)
            {
                it_Allocator.Allocator.Reset();
            }

            m_Queue.Reset();

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
            HRESULT l_Result = m_Queue.Initialize();
            for (std::size_t it_Index = 0; SUCCEEDED(l_Result) && it_Index < m_CommandAllocators.size(); ++it_Index)
            {
                l_Result = m_CommandAllocators[it_Index].Create(m_Device);
            }

            if (SUCCEEDED(l_Result))
            {
                l_Result = m_CommandList.Initialize(m_Device);
            }

            if (FAILED(l_Result))
            {
                error = std::format("the command queue, command lists and fence could not be created on {} ({})", m_Device.GetInfo().AdapterName, FormatResult(l_Result));

                return false;
            }

            if (!m_Queue.CreateFenceEvent() || !m_RenderTargetViews.Initialize(m_Device.GetHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, c_RenderTargetViewCapacity, false) || !m_DepthStencilViews.Initialize(m_Device.GetHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, c_DepthStencilViewCapacity, false))
            {
                error = std::format("the fence event and view heaps could not be created on {}", m_Device.GetInfo().AdapterName);

                return false;
            }

            return true;
        }

        // Shaders index the two heaps directly, so every pipeline shares one root signature that holds only the push constants
        bool D3D12Context::CreateBindless(std::string& error)
        {
            if (!m_ResourceHeap.Initialize(m_Device.GetHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, c_BindlessResourceCapacity, true, L"Trinity bindless resources") || !m_SamplerHeap.Initialize(m_Device.GetHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, c_BindlessSamplerCapacity, true, L"Trinity bindless samplers"))
            {
                error = std::format("the shader-visible descriptor heaps could not be created on {}", m_Device.GetInfo().AdapterName);

                return false;
            }

            if (!m_RootSignature.Initialize(error))
            {
                return false;
            }

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
            std::optional<D3D12Pipeline> l_Pipeline = D3D12Pipeline::CreateGraphics(m_Device, m_RootSignature, description);
            if (!l_Pipeline)
            {
                return {};
            }

            return m_Pipelines.Add(std::move(*l_Pipeline));
        }

        QueryPoolHandle D3D12Context::CreateQueryPool(const QueryPoolDescription& description)
        {
            std::optional<D3D12QueryPool> l_Pool = D3D12QueryPool::Create(m_Device, description);
            if (!l_Pool)
            {
                return {};
            }

            return m_QueryPools.Add(std::move(*l_Pool));
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
                m_Releases.Push(l_Pool->ToRelease());
            }
        }

        std::uint64_t D3D12Context::GetTimestampFrequency() const
        {
            return D3D12QueryPool::GetTimestampFrequency(m_Queue.GetHandle());
        }

        PipelineHandle D3D12Context::CreateComputePipeline(const ComputePipelineDescription& description)
        {
            std::optional<D3D12Pipeline> l_Pipeline = D3D12Pipeline::CreateCompute(m_Device, m_RootSignature, description);
            if (!l_Pipeline)
            {
                return {};
            }

            return m_Pipelines.Add(std::move(*l_Pipeline));
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
                m_Releases.Push(l_Pipeline->ToRelease());
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

            const D3D12CommandAllocator& l_Allocator = m_CommandAllocators[m_FrameNumber % c_FramesInFlight];
            m_Queue.WaitForFence(l_Allocator.CompletionValue);

            m_InFrame = true;
            m_Releases.BeginFrame();
            m_Allocator.BeginFrame(m_FrameNumber);
            m_UploadRing.BeginFrame();
            m_CommandList.Begin(l_Allocator.Allocator.Get());

            return m_CommandList;
        }

        void D3D12Context::EndFrame()
        {
            TR_CORE_ASSERT(m_InFrame, "EndFrame without BeginFrame.");

            D3D12CommandAllocator& l_Allocator = m_CommandAllocators[m_FrameNumber % c_FramesInFlight];
            m_UploadRing.EndFrame();
            m_CommandList.End();

            l_Allocator.CompletionValue = m_Queue.Submit(m_CommandList.GetHandle());

            ++m_FrameNumber;
            m_Releases.EndFrame();
            m_InFrame = false;
        }

        void D3D12Context::WaitIdle()
        {
            m_Queue.WaitIdle();
            m_Releases.ReleaseIdle();
        }
    }
}