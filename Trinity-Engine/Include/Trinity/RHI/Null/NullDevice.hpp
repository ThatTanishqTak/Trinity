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

            void TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after) override;
            void BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after) override;

            void BeginRendering(const RenderingDescription& description) override;
            void EndRendering() override;

            void SetPipeline(PipelineHandle pipeline) override;
            void SetViewport(const Viewport& viewport) override;
            void SetScissor(const Rect& scissor) override;
            void PushConstants(std::span<const std::byte> data) override;
            void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) override;

            void CopyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination, std::uint64_t destinationOffset, std::uint64_t size) override;
            void CopyTextureToBuffer(TextureHandle source, BufferHandle destination) override;

        private:
            NullDevice& m_Device;
            bool m_Recording = false;
            bool m_Rendering = false;
            bool m_HasPipeline = false;
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

            [[nodiscard]] TextureHandle CreateTexture(const TextureDescription& description) override;
            void DestroyTexture(TextureHandle texture) override;

            [[nodiscard]] std::uint32_t GetShaderResourceIndex(BufferHandle buffer) override;
            [[nodiscard]] std::uint32_t GetUnorderedAccessIndex(BufferHandle buffer) override;
            [[nodiscard]] std::uint32_t GetShaderResourceIndex(TextureHandle texture) override;
            [[nodiscard]] std::uint32_t GetUnorderedAccessIndex(TextureHandle texture) override;

            [[nodiscard]] PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescription& description) override;
            void DestroyPipeline(PipelineHandle pipeline) override;

            [[nodiscard]] Scope<SwapChain> CreateSwapChain(const SwapChainSpecification& specification) override;

            [[nodiscard]] CommandList& BeginFrame() override;
            void EndFrame() override;
            void WaitIdle() override;

            [[nodiscard]] bool IsAlive(BufferHandle buffer) { return m_Buffers.Get(buffer) != nullptr; }
            [[nodiscard]] bool IsAlive(TextureHandle texture) { return m_Textures.Get(texture) != nullptr; }
            [[nodiscard]] bool IsAlive(PipelineHandle pipeline) { return m_Pipelines.Get(pipeline) != nullptr; }

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
                Format TextureFormat = Format::Unknown;
                TextureUsage Usage = TextureUsage::None;
            };

            struct NullPipeline
            {

            };

            static void ReleaseBuffer(NullBuffer& buffer);

            DeviceInfo m_Info;
            HandlePool<NullBuffer, BufferHandle> m_Buffers;
            HandlePool<NullTexture, TextureHandle> m_Textures;
            HandlePool<NullPipeline, PipelineHandle> m_Pipelines;
            ReleaseQueue<NullBuffer> m_ReleasedBuffers;
            NullCommandList m_CommandList;
            bool m_InFrame = false;
        };
    }
}