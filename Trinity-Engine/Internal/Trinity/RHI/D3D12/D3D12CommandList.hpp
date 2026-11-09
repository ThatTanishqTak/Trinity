#pragma once

#include "Trinity/RHI/CommandList.hpp"
#include "Trinity/RHI/D3D12/D3D12Headers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Context;

        class D3D12CommandList final : public CommandList
        {
        public:
            explicit D3D12CommandList(D3D12Context& device);

            void Begin(ID3D12GraphicsCommandList7* commandList);
            void End();

            void TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after, const TextureSubresourceRange& range) override;
            void BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after) override;

            void BeginRendering(const RenderingDescription& description) override;
            void EndRendering() override;

            void SetPipeline(PipelineHandle pipeline) override;
            void SetViewport(const Viewport& viewport) override;
            void SetScissor(const Rect& scissor) override;
            void SetStencilReference(std::uint32_t reference) override;
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
            D3D12Context& m_Device;
            ID3D12GraphicsCommandList7* m_CommandList = nullptr;
            // What each resolving attachment's ending access points to, so it lives until EndRenderPass
            std::array<D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS, c_MaxColorAttachments> m_ResolveParameters{};
            bool m_Rendering = false;
            bool m_HasPipeline = false;
            bool m_HasComputePipeline = false;
            bool m_HasIndexBuffer = false;
        };
    }
}