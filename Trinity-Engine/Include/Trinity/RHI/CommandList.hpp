#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/RHI/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

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
            // A single-sampled texture in ResolveDestination that the multisampled one is averaged into when rendering ends
            TextureHandle ResolveTexture{};
        };

        // A format with stencil loads, clears and stores its stencil as it does its depth
        struct DepthAttachment
        {
            TextureHandle Texture;
            LoadOp Load = LoadOp::Clear;
            StoreOp Store = StoreOp::DontCare;
            float ClearDepth = 0.0f;
            std::uint8_t ClearStencil = 0;
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
            // What a pipeline's stencil test compares with and StencilOp::Replace writes, 0 at the start of each frame and kept until set again
            virtual void SetStencilReference(std::uint32_t reference) = 0;
            virtual void PushConstants(std::span<const std::byte> data) = 0;
            virtual void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) = 0;

            virtual void SetIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format) = 0;
            virtual void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::uint32_t firstInstance) = 0;

            virtual void Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) = 0;

            // A timestamp can be written inside rendering or outside it, and each index once between resolves. ResolveTimestamps, outside rendering, writes 8 bytes per timestamp at an offset that is a multiple of 8, into a buffer in CopyDestination
            virtual void WriteTimestamp(QueryPoolHandle pool, std::uint32_t index) = 0;
            virtual void ResolveTimestamps(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count, BufferHandle destination, std::uint64_t destinationOffset) = 0;

            virtual void CopyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination, std::uint64_t destinationOffset, std::uint64_t size) = 0;
            virtual void CopyTextureToBuffer(TextureHandle source, std::uint32_t mipLevel, std::uint32_t arrayLayer, BufferHandle destination, std::uint64_t destinationOffset) = 0;
            virtual void CopyBufferToTexture(BufferHandle source, std::uint64_t sourceOffset, TextureHandle destination, std::uint32_t mipLevel, std::uint32_t arrayLayer, const Rect& region) = 0;

            // A named region of commands for RenderDoc and PIX, which nests and ends with EndLabel within the same frame. TR_GPU_LABEL begins one for the rest of a scope
            virtual void BeginLabel(std::string_view name) = 0;
            virtual void EndLabel() = 0;

        protected:
            CommandList() = default;
        };

        // Begins a label and ends it when it goes out of scope
        class LabelScope
        {
        public:
            LabelScope(CommandList& commands, std::string_view name) : m_Commands(commands)
            {
                m_Commands.BeginLabel(name);
            }

            ~LabelScope()
            {
                m_Commands.EndLabel();
            }

            LabelScope(const LabelScope&) = delete;
            LabelScope& operator=(const LabelScope&) = delete;

        private:
            CommandList& m_Commands;
        };
    }
}

// Labels the commands recorded in the rest of the scope. Distribution compiles each one out, so no label text reaches a player
#if defined(TR_DISTRIBUTION)
#define TR_GPU_LABEL(commands, name) ((void)0)
#else
#define TR_INTERNAL_GPU_LABEL_CONCAT_IMPL(a, b) a##b
#define TR_INTERNAL_GPU_LABEL_CONCAT(a, b) TR_INTERNAL_GPU_LABEL_CONCAT_IMPL(a, b)
#define TR_GPU_LABEL(commands, name) const ::Trinity::RHI::LabelScope TR_INTERNAL_GPU_LABEL_CONCAT(l_GpuLabel, __LINE__)(commands, name)
#endif