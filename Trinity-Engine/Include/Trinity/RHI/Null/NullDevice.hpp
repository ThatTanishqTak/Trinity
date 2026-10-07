#pragma once

#include "Trinity/RHI/Device.hpp"
#include "Trinity/RHI/HandlePool.hpp"
#include "Trinity/RHI/ReleaseQueue.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Trinity
{
    namespace RHI
    {
        class NullDevice;

        // Records nothing, but checks that commands arrive in a valid order with live handles
        class NullCommandList final : public CommandList
        {
        public:
            explicit NullCommandList(NullDevice& device);

            void Begin();
            void End();

            void TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after, const TextureSubresourceRange& range) override;
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

            void WriteTimestamp(QueryPoolHandle pool, std::uint32_t index) override;
            void ResolveTimestamps(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count, BufferHandle destination, std::uint64_t destinationOffset) override;

            void CopyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination, std::uint64_t destinationOffset, std::uint64_t size) override;
            void CopyTextureToBuffer(TextureHandle source, std::uint32_t mipLevel, std::uint32_t arrayLayer, BufferHandle destination, std::uint64_t destinationOffset) override;
            void CopyBufferToTexture(BufferHandle source, std::uint64_t sourceOffset, TextureHandle destination, std::uint32_t mipLevel, std::uint32_t arrayLayer, const Rect& region) override;

        private:
            NullDevice& m_Device;
            bool m_Recording = false;
            bool m_Rendering = false;
            bool m_HasPipeline = false;
            bool m_HasComputePipeline = false;
            bool m_HasIndexBuffer = false;
        };

        class NullSwapChain final : public SwapChain
        {
        public:
            NullSwapChain(NullDevice& device, const SwapChainSpecification& specification);
            ~NullSwapChain() override;

            [[nodiscard]] TextureHandle AcquireNextTexture() override;
            void Present() override;

            void Resize(std::uint32_t width, std::uint32_t height) override;
            void SetVSync(bool enabled) override;

            [[nodiscard]] Format GetFormat() const override { return m_Specification.ImageFormat; }
            [[nodiscard]] std::uint32_t GetWidth() const override { return m_Specification.Width; }
            [[nodiscard]] std::uint32_t GetHeight() const override { return m_Specification.Height; }

        private:
            void CreateTexture();

            NullDevice& m_Device;
            SwapChainSpecification m_Specification;
            TextureHandle m_Texture;
        };

        // The device for headless runs
        class NullDevice final : public Device
        {
        public:
            explicit NullDevice(const DeviceSpecification& specification);
            ~NullDevice() override;

            [[nodiscard]] const DeviceInfo& GetInfo() const override { return m_Info; }

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
            [[nodiscard]] std::uint64_t GetTimestampFrequency() const override { return 1000000000; }

            [[nodiscard]] PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescription& description) override;
            [[nodiscard]] PipelineHandle CreateComputePipeline(const ComputePipelineDescription& description) override;
            void DestroyPipeline(PipelineHandle pipeline) override;

            [[nodiscard]] Scope<SwapChain> CreateSwapChain(const SwapChainSpecification& specification) override;

            [[nodiscard]] CommandList& BeginFrame() override;
            void EndFrame() override;
            void WaitIdle() override;

            [[nodiscard]] bool IsAlive(BufferHandle buffer) { return m_Buffers.Get(buffer) != nullptr; }
            [[nodiscard]] bool IsAlive(TextureHandle texture) { return m_Textures.Get(texture) != nullptr; }
            [[nodiscard]] bool IsAlive(PipelineHandle pipeline) { return m_Pipelines.Get(pipeline) != nullptr; }
            [[nodiscard]] bool IsComputePipeline(PipelineHandle pipeline);
            [[nodiscard]] bool IsQueryRangeValid(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count);
            [[nodiscard]] bool IsAlive(SamplerHandle sampler) { return m_Samplers.Get(sampler) != nullptr; }
            [[nodiscard]] bool IsIndexRangeValid(BufferHandle buffer, std::uint64_t offset, IndexFormat format);
            [[nodiscard]] bool IsRangeValid(TextureHandle texture, const TextureSubresourceRange& range);
            [[nodiscard]] bool IsAttachmentValid(TextureHandle texture, TextureUsage usage, std::uint32_t mipLevel, std::uint32_t arrayLayer);
            [[nodiscard]] bool IsResolveValid(TextureHandle source, TextureHandle destination);
            [[nodiscard]] bool IsCopyRegionValid(BufferHandle source, std::uint64_t sourceOffset, TextureHandle destination, std::uint32_t mipLevel, std::uint32_t arrayLayer, const Rect& region);
            [[nodiscard]] bool IsReadbackValid(TextureHandle source, std::uint32_t mipLevel, std::uint32_t arrayLayer, BufferHandle destination, std::uint64_t destinationOffset);

        private:
            struct NullBuffer
            {
                std::uint64_t Size = 0;
                std::byte* Mapped = nullptr;
                BufferUsage Usage = BufferUsage::None;
            };

            struct NullTexture
            {
                std::uint32_t Width = 0;
                std::uint32_t Height = 0;
                std::uint32_t MipLevels = 0;
                std::uint32_t ArrayLayers = 0;
                std::uint32_t SampleCount = 1;
                Format TextureFormat = Format::Unknown;
                TextureUsage Usage = TextureUsage::None;
            };

            struct NullPipeline
            {
                bool Compute = false;
            };

            struct NullSampler
            {

            };

            struct NullQueryPool
            {
                std::uint32_t Count = 0;
            };

            static void ReleaseBuffer(NullBuffer& buffer);

            DeviceInfo m_Info;
            HandlePool<NullBuffer, BufferHandle> m_Buffers;
            HandlePool<NullTexture, TextureHandle> m_Textures;
            HandlePool<NullPipeline, PipelineHandle> m_Pipelines;
            HandlePool<NullSampler, SamplerHandle> m_Samplers;
            HandlePool<NullQueryPool, QueryPoolHandle> m_QueryPools;
            ReleaseQueue<NullBuffer> m_ReleasedBuffers;
            NullCommandList m_CommandList;
            bool m_InFrame = false;
        };
    }
}