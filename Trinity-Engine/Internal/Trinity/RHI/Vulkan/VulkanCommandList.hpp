#pragma once

#include "Trinity/RHI/CommandList.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class VulkanContext;

        // The frame's commands on the graphics queue, or with uploads the copies and barriers the loaders record for the transfer queue
        class VulkanCommandList final : public CommandList
        {
        public:
            explicit VulkanCommandList(VulkanContext& device, bool uploads = false);

            void Begin(VkCommandBuffer commandBuffer);
            void End();

            // Whether a copy or barrier was recorded since Begin, so the list is worth submitting
            [[nodiscard]] bool HasCommands() const { return m_HasCommands; }
            // Whether the uploads write something the frame before may still be reading, so they wait for it
            [[nodiscard]] bool WaitsForGraphics() const { return m_WaitsForGraphics; }
            // The barriers after the uploads' copies into the frame's list, which waits for the uploads before it runs
            void RecordHandoffs(VulkanCommandList& frame);

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

            void BeginLabel(std::string_view name) override;
            void EndLabel() override;

        private:
            template<typename Barrier>
            [[nodiscard]] bool HandOff(Barrier& barrier, ResourceState before, ResourceState after, std::vector<Barrier>& handoffs);

            VulkanContext& m_Device;
            VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
            bool m_Rendering = false;
            bool m_HasPipeline = false;
            bool m_HasComputePipeline = false;
            bool m_HasIndexBuffer = false;

            const bool m_Uploads = false;
            bool m_HasCommands = false;
            bool m_WaitsForGraphics = false;
            std::vector<VkImageMemoryBarrier2> m_ImageHandoffs;
            std::vector<VkBufferMemoryBarrier2> m_BufferHandoffs;
        };
    }
}