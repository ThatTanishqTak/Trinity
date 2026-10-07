#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/RHI/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Trinity
{
    namespace RHI
    {
        enum class LoadOp : std::uint8_t
        {
            Load,
            Clear,
            DontCare
        };

        enum class StoreOp : std::uint8_t
        {
            Store,
            DontCare
        };

        struct ColorAttachment
        {
            TextureHandle Texture;
            LoadOp Load = LoadOp::Clear;
            StoreOp Store = StoreOp::Store;
            std::array<float, 4> ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
            std::uint32_t MipLevel = 0;
            std::uint32_t ArrayLayer = 0;
        };

        struct DepthAttachment
        {
            TextureHandle Texture;
            LoadOp Load = LoadOp::Clear;
            StoreOp Store = StoreOp::DontCare;
            float ClearDepth = 0.0f;
            std::uint32_t MipLevel = 0;
            std::uint32_t ArrayLayer = 0;
        };

        struct RenderingDescription
        {
            std::span<const ColorAttachment> ColorAttachments;
            DepthAttachment Depth;
            Rect RenderArea;
        };

        class TRINITY_API CommandList
        {
        public:
            virtual ~CommandList() = default;

            CommandList(const CommandList&) = delete;
            CommandList& operator=(const CommandList&) = delete;

            virtual void TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after, const TextureSubresourceRange& range = {}) = 0;
            virtual void BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after) = 0;

            virtual void BeginRendering(const RenderingDescription& description) = 0;
            virtual void EndRendering() = 0;

            virtual void SetPipeline(PipelineHandle pipeline) = 0;
            virtual void SetViewport(const Viewport& viewport) = 0;
            virtual void SetScissor(const Rect& scissor) = 0;
            virtual void PushConstants(std::span<const std::byte> data) = 0;
            virtual void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) = 0;

            virtual void SetIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format) = 0;
            virtual void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::uint32_t firstInstance) = 0;

            virtual void Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) = 0;

            virtual void CopyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination, std::uint64_t destinationOffset, std::uint64_t size) = 0;
            virtual void CopyTextureToBuffer(TextureHandle source, std::uint32_t mipLevel, std::uint32_t arrayLayer, BufferHandle destination, std::uint64_t destinationOffset) = 0;
            virtual void CopyBufferToTexture(BufferHandle source, std::uint64_t sourceOffset, TextureHandle destination, std::uint32_t mipLevel, std::uint32_t arrayLayer, const Rect& region) = 0;

        protected:
            CommandList() = default;
        };
    }
}