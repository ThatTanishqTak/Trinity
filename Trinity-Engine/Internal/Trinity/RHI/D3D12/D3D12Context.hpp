#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Bindless.hpp"
#include "Trinity/RHI/D3D12/D3D12Allocator.hpp"
#include "Trinity/RHI/D3D12/D3D12CommandList.hpp"
#include "Trinity/RHI/D3D12/D3D12DescriptorHeap.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#include "Trinity/RHI/D3D12/D3D12Factory.hpp"
#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/D3D12/D3D12ReleaseQueue.hpp"
#include "Trinity/RHI/D3D12/D3D12RootSignature.hpp"
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
        struct D3D12Buffer
        {
            Microsoft::WRL::ComPtr<D3D12MA::Allocation> Allocation;
            std::byte* Mapped = nullptr;
            std::uint64_t Size = 0;
            std::uint32_t ShaderResourceIndex = c_NoDescriptor;
            std::uint32_t UnorderedAccessIndex = c_NoDescriptor;
        };

        // Swap chain buffers have no allocation, since the swap chain owns them. A render target or depth texture has a view per mip of each layer, numbered by GetSubresourceIndex
        struct D3D12Texture
        {
            Microsoft::WRL::ComPtr<D3D12MA::Allocation> Allocation;
            ID3D12Resource* Resource = nullptr;
            Format TextureFormat = Format::Unknown;
            DXGI_FORMAT ResourceFormat = DXGI_FORMAT_UNKNOWN;
            TextureDimension Dimension = TextureDimension::Texture2D;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
            std::uint32_t MipLevels = 0;
            std::uint32_t ArrayLayers = 0;
            std::uint32_t SampleCount = 1;
            D3D12DescriptorIndices RenderTargetViews;
            D3D12DescriptorIndices DepthStencilViews;
            std::uint32_t ShaderResourceIndex = c_NoDescriptor;
            std::uint32_t UnorderedAccessIndex = c_NoDescriptor;
        };

        struct D3D12Pipeline
        {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> State;
            D3D_PRIMITIVE_TOPOLOGY Topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
            bool Compute = false;
        };

        struct D3D12QueryPool
        {
            Microsoft::WRL::ComPtr<ID3D12QueryHeap> Heap;
            std::uint32_t Count = 0;
        };

        // A sampler is only its descriptor in the shader-visible sampler heap
        struct D3D12Sampler
        {
            std::uint32_t Index = c_NoDescriptor;
        };

        class D3D12Context final : public Device
        {
        public:
            // Null, with the reason in error, when no adapter can run the engine
            [[nodiscard]] static Scope<D3D12Context> Create(const DeviceSpecification& specification, std::string& error);

            D3D12Context();
            ~D3D12Context() override;

            [[nodiscard]] const DeviceInfo& GetInfo() const override { return m_Device.GetInfo(); }

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
            [[nodiscard]] ID3D12CommandQueue* GetQueue() const { return m_Queue.Get(); }
            [[nodiscard]] bool IsTearingSupported() const { return m_Factory.IsTearingSupported(); }
            [[nodiscard]] bool IsInFrame() const { return m_InFrame; }

        private:
            struct FrameContext
            {
                Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator;
                std::uint64_t CompletionValue = 0;
            };

            [[nodiscard]] bool Initialize(const DeviceSpecification& specification, std::string& error);
            [[nodiscard]] bool CreateFrames(std::string& error);
            [[nodiscard]] bool CreateBindless(std::string& error);

            void CreateViews(D3D12Texture& texture);
            void CreateShaderViews(D3D12Buffer& buffer, BufferUsage usage);
            void CreateShaderViews(D3D12Texture& texture, TextureUsage usage);
            void WaitForFence(std::uint64_t value);
            void WaitForQueue();

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

            Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_Queue;
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> m_GraphicsList;
            std::array<FrameContext, c_FramesInFlight> m_Frames{};
            Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
            HANDLE m_FenceEvent = nullptr;
            std::uint64_t m_FrameNumber = 0;
            std::uint64_t m_FenceValue = 0;

            D3D12CommandList m_CommandList;
            bool m_InFrame = false;
        };
    }
}