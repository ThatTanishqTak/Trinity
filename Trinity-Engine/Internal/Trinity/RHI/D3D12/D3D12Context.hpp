#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Bindless.hpp"
#include "Trinity/RHI/D3D12/D3D12Allocator.hpp"
#include "Trinity/RHI/D3D12/D3D12Buffer.hpp"
#include "Trinity/RHI/D3D12/D3D12CommandAllocator.hpp"
#include "Trinity/RHI/D3D12/D3D12CommandList.hpp"
#include "Trinity/RHI/D3D12/D3D12CommandQueue.hpp"
#include "Trinity/RHI/D3D12/D3D12DescriptorHeap.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#include "Trinity/RHI/D3D12/D3D12Factory.hpp"
#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/D3D12/D3D12Pipeline.hpp"
#include "Trinity/RHI/D3D12/D3D12QueryPool.hpp"
#include "Trinity/RHI/D3D12/D3D12ReleaseQueue.hpp"
#include "Trinity/RHI/D3D12/D3D12RootSignature.hpp"
#include "Trinity/RHI/D3D12/D3D12Sampler.hpp"
#include "Trinity/RHI/D3D12/D3D12Texture.hpp"
#include "Trinity/RHI/Device.hpp"
#include "Trinity/RHI/HandlePool.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Context final : public Device
        {
        public:
            // Null, with the reason in error, when no adapter can run the engine
            [[nodiscard]] static Scope<D3D12Context> Create(const DeviceSpecification& specification, std::string& error);

            D3D12Context();
            ~D3D12Context() override;

            [[nodiscard]] const DeviceInfo& GetInfo() const override { return m_Device.GetInfo(); }
            [[nodiscard]] std::vector<MemoryHeapBudget> GetMemoryBudget() const override { return m_Allocator.GetMemoryBudget(); }

            [[nodiscard]] BufferHandle CreateBuffer(const BufferDescription& description) override;
            void DestroyBuffer(BufferHandle buffer) override;
            [[nodiscard]] std::span<std::byte> GetMappedData(BufferHandle buffer) override;

            [[nodiscard]] bool IsFormatSupported(Format format, TextureUsage usage, std::uint32_t sampleCount) const override;
            [[nodiscard]] TextureHandle CreateTexture(const TextureDescription& description) override;
            void DestroyTexture(TextureHandle texture) override;

            [[nodiscard]] std::uint32_t GetShaderResourceIndex(BufferHandle buffer) override;
            [[nodiscard]] std::uint32_t GetUnorderedAccessIndex(BufferHandle buffer) override;
            [[nodiscard]] std::uint32_t GetShaderResourceIndex(TextureHandle texture) override;
            [[nodiscard]] std::uint32_t GetUnorderedAccessIndex(TextureHandle texture) override;

            [[nodiscard]] SamplerHandle CreateSampler(const SamplerDescription& description) override;
            void DestroySampler(SamplerHandle sampler) override;
            [[nodiscard]] std::uint32_t GetSamplerIndex(SamplerHandle sampler) override;

            [[nodiscard]] QueryPoolHandle CreateQueryPool(const QueryPoolDescription& description) override;
            void DestroyQueryPool(QueryPoolHandle pool) override;
            [[nodiscard]] std::uint64_t GetTimestampFrequency() const override;

            [[nodiscard]] PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescription& description) override;
            [[nodiscard]] PipelineHandle CreateComputePipeline(const ComputePipelineDescription& description) override;
            void DestroyPipeline(PipelineHandle pipeline) override;

            [[nodiscard]] Scope<SwapChain> CreateSwapChain(const SwapChainSpecification& specification) override;

            [[nodiscard]] CommandList& BeginFrame() override;
            void EndFrame() override;
            void WaitIdle() override;

            [[nodiscard]] D3D12Buffer* GetBuffer(BufferHandle buffer) { return m_Buffers.Get(buffer); }
            [[nodiscard]] D3D12Texture* GetTexture(TextureHandle texture) { return m_Textures.Get(texture); }
            [[nodiscard]] D3D12Pipeline* GetPipeline(PipelineHandle pipeline) { return m_Pipelines.Get(pipeline); }
            [[nodiscard]] D3D12QueryPool* GetQueryPool(QueryPoolHandle pool) { return m_QueryPools.Get(pool); }
            [[nodiscard]] ID3D12RootSignature* GetRootSignature() const { return m_RootSignature.GetHandle(); }
            [[nodiscard]] ID3D12DescriptorHeap* GetResourceHeap() const { return m_ResourceHeap.GetHeap(); }
            [[nodiscard]] ID3D12DescriptorHeap* GetSamplerHeap() const { return m_SamplerHeap.GetHeap(); }
            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView(std::uint32_t view) const { return m_RenderTargetViews.GetHandle(view); }
            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView(std::uint32_t view) const { return m_DepthStencilViews.GetHandle(view); }

            // For the swap chain, whose buffers become textures with a render target view each
            [[nodiscard]] TextureHandle AddSwapChainBuffer(ID3D12Resource* resource, Format format, DXGI_FORMAT resourceFormat, std::uint32_t width, std::uint32_t height);
            void RemoveSwapChainBuffer(TextureHandle texture);

            [[nodiscard]] IDXGIFactory6* GetFactory() const { return m_Factory.GetHandle(); }
            [[nodiscard]] ID3D12CommandQueue* GetQueue() const { return m_Queue.GetHandle(); }
            [[nodiscard]] bool IsTearingSupported() const { return m_Factory.IsTearingSupported(); }
            [[nodiscard]] bool IsInFrame() const { return m_InFrame; }

        private:
            [[nodiscard]] bool Initialize(const DeviceSpecification& specification, std::string& error);
            [[nodiscard]] bool CreateFrames(std::string& error);
            [[nodiscard]] bool CreateBindless(std::string& error);

            D3D12Factory m_Factory;
            D3D12Device m_Device;
            D3D12Allocator m_Allocator;

            HandlePool<D3D12Buffer, BufferHandle> m_Buffers;
            HandlePool<D3D12Texture, TextureHandle> m_Textures;
            HandlePool<D3D12Pipeline, PipelineHandle> m_Pipelines;
            HandlePool<D3D12Sampler, SamplerHandle> m_Samplers;
            HandlePool<D3D12QueryPool, QueryPoolHandle> m_QueryPools;
            D3D12DescriptorHeap m_RenderTargetViews;
            D3D12DescriptorHeap m_DepthStencilViews;
            D3D12DescriptorHeap m_ResourceHeap;
            D3D12DescriptorHeap m_SamplerHeap;
            D3D12RootSignature m_RootSignature;
            D3D12ReleaseQueue m_Releases;

            D3D12CommandQueue m_Queue;
            std::array<D3D12CommandAllocator, c_FramesInFlight> m_CommandAllocators{};
            std::uint64_t m_FrameNumber = 0;

            D3D12CommandList m_CommandList;
            bool m_InFrame = false;
        };
    }
}