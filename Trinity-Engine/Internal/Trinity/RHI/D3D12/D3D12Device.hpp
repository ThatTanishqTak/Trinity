#pragma once

#include "Trinity/RHI/Bindless.hpp"
#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/Device.hpp"
#include "Trinity/RHI/HandlePool.hpp"
#include "Trinity/RHI/ReleaseQueue.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Device;

        constexpr std::uint32_t c_NoDescriptor = c_NoBindlessIndex;

        struct D3D12Buffer
        {
            Microsoft::WRL::ComPtr<D3D12MA::Allocation> Allocation;
            std::byte* Mapped = nullptr;
            std::uint64_t Size = 0;
            std::uint32_t ShaderResourceIndex = c_NoDescriptor;
            std::uint32_t UnorderedAccessIndex = c_NoDescriptor;
        };

        // Swap chain buffers have no allocation, since the swap chain owns them
        struct D3D12Texture
        {
            Microsoft::WRL::ComPtr<D3D12MA::Allocation> Allocation;
            ID3D12Resource* Resource = nullptr;
            Format TextureFormat = Format::Unknown;
            DXGI_FORMAT ResourceFormat = DXGI_FORMAT_UNKNOWN;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
            std::uint32_t MipLevels = 0;
            std::uint32_t RenderTargetView = c_NoDescriptor;
            std::uint32_t DepthStencilView = c_NoDescriptor;
            std::uint32_t ShaderResourceIndex = c_NoDescriptor;
            std::uint32_t UnorderedAccessIndex = c_NoDescriptor;
        };

        struct D3D12Pipeline
        {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> State;
            D3D_PRIMITIVE_TOPOLOGY Topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
            bool Compute = false;
        };

        // A sampler is only its descriptor in the shader-visible sampler heap
        struct D3D12Sampler
        {
            std::uint32_t Index = c_NoDescriptor;
        };

        // A heap of descriptors handed out a slot at a time, CPU-only for render target and depth views and shader-visible for the bindless heaps
        class D3D12DescriptorHeap
        {
        public:
            [[nodiscard]] bool Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t capacity, bool shaderVisible);
            void Reset();

            [[nodiscard]] std::uint32_t Allocate();
            void Free(std::uint32_t index);

            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetHandle(std::uint32_t index) const;
            [[nodiscard]] ID3D12DescriptorHeap* GetHeap() const { return m_Heap.Get(); }

        private:
            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_Heap;
            D3D12_CPU_DESCRIPTOR_HANDLE m_Start{};
            std::uint32_t m_Increment = 0;
            IndexAllocator m_Indices;
        };

        class D3D12CommandList final : public CommandList
        {
        public:
            explicit D3D12CommandList(D3D12Device& device);

            void Begin(ID3D12GraphicsCommandList7* commandList);
            void End();

            void TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after) override;
            void BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after) override;

            void BeginRendering(const RenderingDescription& description) override;
            void EndRendering() override;

            void SetPipeline(PipelineHandle pipeline) override;
            void SetViewport(const Viewport& viewport) override;
            void SetScissor(const Rect& scissor) override;
            void PushConstants(std::span<const std::byte> data) override;
            void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) override;
            void SetIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format) override;
            void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::uint32_t firstInstance) override;
            void Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) override;

            void CopyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination, std::uint64_t destinationOffset, std::uint64_t size) override;
            void CopyTextureToBuffer(TextureHandle source, std::uint32_t mipLevel, BufferHandle destination, std::uint64_t destinationOffset) override;
            void CopyBufferToTexture(BufferHandle source, std::uint64_t sourceOffset, TextureHandle destination, std::uint32_t mipLevel, const Rect& region) override;

        private:
            D3D12Device& m_Device;
            ID3D12GraphicsCommandList7* m_CommandList = nullptr;
            bool m_Rendering = false;
            bool m_HasPipeline = false;
            bool m_HasComputePipeline = false;
            bool m_HasIndexBuffer = false;
        };

        class D3D12Device final : public Device
        {
        public:
            // Null, with the reason in error, when no adapter can run the engine
            [[nodiscard]] static Scope<D3D12Device> Create(const DeviceSpecification& specification, std::string& error);

            D3D12Device();
            ~D3D12Device() override;

            [[nodiscard]] const DeviceInfo& GetInfo() const override { return m_Info; }

            [[nodiscard]] BufferHandle CreateBuffer(const BufferDescription& description) override;
            void DestroyBuffer(BufferHandle buffer) override;
            [[nodiscard]] std::span<std::byte> GetMappedData(BufferHandle buffer) override;

            [[nodiscard]] bool IsFormatSupported(Format format, TextureUsage usage) const override;
            [[nodiscard]] TextureHandle CreateTexture(const TextureDescription& description) override;
            void DestroyTexture(TextureHandle texture) override;

            [[nodiscard]] std::uint32_t GetShaderResourceIndex(BufferHandle buffer) override;
            [[nodiscard]] std::uint32_t GetUnorderedAccessIndex(BufferHandle buffer) override;
            [[nodiscard]] std::uint32_t GetShaderResourceIndex(TextureHandle texture) override;
            [[nodiscard]] std::uint32_t GetUnorderedAccessIndex(TextureHandle texture) override;

            [[nodiscard]] SamplerHandle CreateSampler(const SamplerDescription& description) override;
            void DestroySampler(SamplerHandle sampler) override;
            [[nodiscard]] std::uint32_t GetSamplerIndex(SamplerHandle sampler) override;

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
            [[nodiscard]] ID3D12RootSignature* GetRootSignature() const { return m_RootSignature.Get(); }
            [[nodiscard]] ID3D12DescriptorHeap* GetResourceHeap() const { return m_ResourceHeap.GetHeap(); }
            [[nodiscard]] ID3D12DescriptorHeap* GetSamplerHeap() const { return m_SamplerHeap.GetHeap(); }
            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView(const D3D12Texture& texture) const { return m_RenderTargetViews.GetHandle(texture.RenderTargetView); }
            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView(const D3D12Texture& texture) const { return m_DepthStencilViews.GetHandle(texture.DepthStencilView); }

            // For the swap chain, whose buffers become textures with a render target view each
            [[nodiscard]] TextureHandle AddSwapChainBuffer(ID3D12Resource* resource, Format format, DXGI_FORMAT resourceFormat, std::uint32_t width, std::uint32_t height);
            void RemoveSwapChainBuffer(TextureHandle texture);

            [[nodiscard]] IDXGIFactory6* GetFactory() const { return m_Factory.Get(); }
            [[nodiscard]] ID3D12CommandQueue* GetQueue() const { return m_Queue.Get(); }
            [[nodiscard]] bool IsTearingSupported() const { return m_TearingSupported; }
            [[nodiscard]] bool IsInFrame() const { return m_InFrame; }

        private:
            struct D3D12Release
            {
                Microsoft::WRL::ComPtr<D3D12MA::Allocation> Allocation;
                Microsoft::WRL::ComPtr<ID3D12PipelineState> Pipeline;
                std::uint32_t RenderTargetView = c_NoDescriptor;
                std::uint32_t DepthStencilView = c_NoDescriptor;
                std::uint32_t ShaderResourceIndex = c_NoDescriptor;
                std::uint32_t UnorderedAccessIndex = c_NoDescriptor;
                std::uint32_t SamplerIndex = c_NoDescriptor;
            };

            struct FrameContext
            {
                Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator;
                std::uint64_t CompletionValue = 0;
            };

            static void CALLBACK OnDebugMessage(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context);

            [[nodiscard]] bool Initialize(const DeviceSpecification& specification, std::string& error);
            [[nodiscard]] bool CreateAllocator(std::string& error);
            [[nodiscard]] bool CreateFrames(std::string& error);
            [[nodiscard]] bool CreateBindless(std::string& error);
            void EnableDebugMessages();
            void LogRuntime() const;
            void ReportLiveObjects() const;

            void CreateViews(D3D12Texture& texture);
            void CreateShaderViews(D3D12Buffer& buffer, BufferUsage usage);
            void CreateShaderViews(D3D12Texture& texture, TextureUsage usage);
            void Release(D3D12Release& release);
            void WaitForFence(std::uint64_t value);
            void WaitForQueue();

            DeviceInfo m_Info;
            bool m_Validation = false;

            Microsoft::WRL::ComPtr<IDXGIFactory6> m_Factory;
            Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
            Microsoft::WRL::ComPtr<ID3D12Device10> m_Device;
            Microsoft::WRL::ComPtr<ID3D12InfoQueue1> m_InfoQueue;
            DWORD m_MessageCallbackCookie = 0;
            std::atomic<std::uint32_t> m_MessageCount{ 0 };
            Microsoft::WRL::ComPtr<D3D12MA::Allocator> m_Allocator;

            HandlePool<D3D12Buffer, BufferHandle> m_Buffers;
            HandlePool<D3D12Texture, TextureHandle> m_Textures;
            HandlePool<D3D12Pipeline, PipelineHandle> m_Pipelines;
            HandlePool<D3D12Sampler, SamplerHandle> m_Samplers;
            ReleaseQueue<D3D12Release> m_Releases;
            D3D12DescriptorHeap m_RenderTargetViews;
            D3D12DescriptorHeap m_DepthStencilViews;
            D3D12DescriptorHeap m_ResourceHeap;
            D3D12DescriptorHeap m_SamplerHeap;
            Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;

            Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_Queue;
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> m_GraphicsList;
            std::array<FrameContext, c_FramesInFlight> m_Frames{};
            Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
            HANDLE m_FenceEvent = nullptr;
            std::uint64_t m_FrameNumber = 0;
            std::uint64_t m_FenceValue = 0;
            bool m_TearingSupported = false;

            D3D12CommandList m_CommandList;
            bool m_InFrame = false;
        };
    }
}