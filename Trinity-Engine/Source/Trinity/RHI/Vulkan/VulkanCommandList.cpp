#include "Trinity/RHI/Vulkan/VulkanCommandList.hpp"
#include "Trinity/RHI/Vulkan/VulkanContext.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Assert.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            struct VulkanState
            {
                VkPipelineStageFlags2 Stage = VK_PIPELINE_STAGE_2_NONE;
                VkAccessFlags2 Access = VK_ACCESS_2_NONE;
                VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
            };

            constexpr VkPipelineStageFlags2 c_ShaderStages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            constexpr VkPipelineStageFlags2 c_DepthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

            // Undefined waits on all earlier writes, since the layout change that discards the contents is itself a write, and covering all commands also chains it onto a swap chain's acquire semaphore
            VulkanState ToVulkanState(ResourceState state)
            {
                switch (state)
                {
                    case ResourceState::Undefined:
                    {
                        return { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
                    }
                    case ResourceState::Present:
                    {
                        return { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
                    }
                    case ResourceState::RenderTarget:
                    {
                        return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
                    }
                    case ResourceState::ResolveDestination:
                    {
                        return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
                    }
                    case ResourceState::DepthWrite:
                    {
                        return { c_DepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
                    }
                    case ResourceState::DepthRead:
                    {
                        return { c_DepthStages | c_ShaderStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
                    }
                    case ResourceState::ShaderResource:
                    {
                        return { c_ShaderStages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                    }
                    case ResourceState::UnorderedAccess:
                    {
                        return { c_ShaderStages, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL };
                    }
                    case ResourceState::CopySource:
                    {
                        return { VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
                    }
                    case ResourceState::CopyDestination:
                    {
                        return { VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL };
                    }
                    case ResourceState::IndexBuffer:
                    {
                        return { VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
                    }
                    case ResourceState::IndirectArgument:
                    {
                        return { VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
                    }
                    case ResourceState::Geometry:
                    {
                        return { c_ShaderStages | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
                    }
                }

                return {};
            }

            // Null when the texture has no render target or depth view, or no such mip or layer
            VkImageView GetAttachmentView(const VulkanTexture& texture, std::uint32_t mipLevel, std::uint32_t arrayLayer)
            {
                if (mipLevel >= texture.MipLevels || arrayLayer >= texture.ArrayLayers)
                {
                    return VK_NULL_HANDLE;
                }

                const std::size_t l_Index = GetSubresourceIndex(mipLevel, arrayLayer, texture.MipLevels);

                return l_Index < texture.AttachmentViews.size() ? texture.AttachmentViews[l_Index] : VK_NULL_HANDLE;
            }

            VkAttachmentLoadOp ToVkLoadOp(LoadOp load)
            {
                switch (load)
                {
                    case LoadOp::Load:
                    {
                        return VK_ATTACHMENT_LOAD_OP_LOAD;
                    }
                    case LoadOp::Clear:
                    {
                        return VK_ATTACHMENT_LOAD_OP_CLEAR;
                    }
                    default:
                    {
                        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    }
                }
            }

            VkAttachmentStoreOp ToVkStoreOp(StoreOp store)
            {
                return store == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
            }

            bool IsCopyState(ResourceState state)
            {
                return state == ResourceState::CopySource || state == ResourceState::CopyDestination;
            }
        }

        VulkanCommandList::VulkanCommandList(VulkanContext& device, bool uploads) : m_Device(device), m_Uploads(uploads)
        {

        }

        // A transfer family's commands bind no descriptors, so uploads begin with nothing bound
        void VulkanCommandList::Begin(VkCommandBuffer commandBuffer)
        {
            VkCommandBufferBeginInfo l_Begin = MakeInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
            l_Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(commandBuffer, &l_Begin);

            m_CommandBuffer = commandBuffer;
            m_Rendering = false;
            m_HasPipeline = false;
            m_HasComputePipeline = false;
            m_HasIndexBuffer = false;
            m_HasCommands = false;
            m_WaitsForGraphics = false;
            m_ImageHandoffs.clear();
            m_BufferHandoffs.clear();
            if (m_Uploads)
            {
                return;
            }

            const VkDescriptorSet l_Set = m_Device.GetBindlessSet();
            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Device.GetPipelineLayout(), 0, 1, &l_Set, 0, nullptr);
            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_Device.GetPipelineLayout(), 0, 1, &l_Set, 0, nullptr);
            vkCmdSetStencilReference(m_CommandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
        }

        void VulkanCommandList::End()
        {
            TR_CORE_ASSERT(!m_Rendering, "The frame ended inside BeginRendering.");

            vkEndCommandBuffer(m_CommandBuffer);
            m_CommandBuffer = VK_NULL_HANDLE;
        }

        void VulkanCommandList::RecordHandoffs(VulkanCommandList& frame)
        {
            if (!m_ImageHandoffs.empty() || !m_BufferHandoffs.empty())
            {
                VkDependencyInfo l_Dependency = MakeInfo<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
                l_Dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(m_ImageHandoffs.size());
                l_Dependency.pImageMemoryBarriers = m_ImageHandoffs.data();
                l_Dependency.bufferMemoryBarrierCount = static_cast<std::uint32_t>(m_BufferHandoffs.size());
                l_Dependency.pBufferMemoryBarriers = m_BufferHandoffs.data();
                vkCmdPipelineBarrier2(frame.m_CommandBuffer, &l_Dependency);
            }

            m_ImageHandoffs.clear();
            m_BufferHandoffs.clear();
        }

        // An upload list makes the moves into copy states itself, and leaves the move after its copies to the frame's list, whose stages a transfer queue lacks. An image is handed over from a transfer family of its own, released here and acquired there, while buffers are shared by both families. Out of a state the frame before may be reading, the uploads wait for it instead, which is all the order a buffer needs. False when the barrier is recorded here as it is
        template<typename Barrier>
        bool VulkanCommandList::HandOff(Barrier& barrier, ResourceState before, ResourceState after, std::vector<Barrier>& handoffs)
        {
            if (!m_Uploads)
            {
                return false;
            }

            m_HasCommands = true;
            if (IsCopyState(after))
            {
                if (before == ResourceState::Undefined || IsCopyState(before))
                {
                    return false;
                }

                m_WaitsForGraphics = true;
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                barrier.srcAccessMask = VK_ACCESS_2_NONE;

                return std::is_same_v<Barrier, VkBufferMemoryBarrier2>;
            }

            if constexpr (std::is_same_v<Barrier, VkImageMemoryBarrier2>)
            {
                if (m_Device.GetTransferFamily() != m_Device.GetQueueFamily())
                {
                    barrier.srcQueueFamilyIndex = m_Device.GetTransferFamily();
                    barrier.dstQueueFamilyIndex = m_Device.GetQueueFamily();

                    VkImageMemoryBarrier2 l_Release = barrier;
                    l_Release.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
                    l_Release.dstAccessMask = VK_ACCESS_2_NONE;

                    VkDependencyInfo l_Dependency = MakeInfo<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
                    l_Dependency.imageMemoryBarrierCount = 1;
                    l_Dependency.pImageMemoryBarriers = &l_Release;
                    vkCmdPipelineBarrier2(m_CommandBuffer, &l_Dependency);

                    barrier.srcAccessMask = VK_ACCESS_2_NONE;
                }
            }

            handoffs.push_back(barrier);

            return true;
        }

        void VulkanCommandList::TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after, const TextureSubresourceRange& range)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "Barriers are recorded within a frame and outside rendering.");
            TR_CORE_ASSERT(after != ResourceState::Undefined, "A texture cannot move into the undefined state.");

            const VulkanTexture* l_Texture = m_Device.GetTexture(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "TextureBarrier on a destroyed or invalid texture.");
            if (l_Texture == nullptr)
            {
                return;
            }

            TR_CORE_ASSERT(IsRangeInsideTexture(range, l_Texture->MipLevels, l_Texture->ArrayLayers), "TextureBarrier on mips or layers that a texture with {} mip(s) and {} layer(s) lacks.", l_Texture->MipLevels, l_Texture->ArrayLayers);

            const VulkanState l_Before = ToVulkanState(before);
            const VulkanState l_After = ToVulkanState(after);

            VkImageMemoryBarrier2 l_Barrier = MakeInfo<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
            l_Barrier.srcStageMask = l_Before.Stage;
            l_Barrier.srcAccessMask = l_Before.Access;
            l_Barrier.dstStageMask = l_After.Stage;
            l_Barrier.dstAccessMask = l_After.Access;
            l_Barrier.oldLayout = l_Before.Layout;
            l_Barrier.newLayout = l_After.Layout;
            l_Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_Barrier.image = l_Texture->Image;
            l_Barrier.subresourceRange = { GetAspect(l_Texture->TextureFormat), range.BaseMipLevel, range.MipLevelCount == c_RemainingSubresources ? VK_REMAINING_MIP_LEVELS : range.MipLevelCount, range.BaseArrayLayer, range.ArrayLayerCount == c_RemainingSubresources ? VK_REMAINING_ARRAY_LAYERS : range.ArrayLayerCount };
            TR_CORE_ASSERT(!m_Uploads || !IsCopyState(after) || before == ResourceState::Undefined || IsCopyState(before), "An uploaded texture is new, so it moves into a copy state from Undefined.");
            if (HandOff(l_Barrier, before, after, m_ImageHandoffs))
            {
                return;
            }

            VkDependencyInfo l_Dependency = MakeInfo<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
            l_Dependency.imageMemoryBarrierCount = 1;
            l_Dependency.pImageMemoryBarriers = &l_Barrier;
            vkCmdPipelineBarrier2(m_CommandBuffer, &l_Dependency);
        }

        void VulkanCommandList::BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "Barriers are recorded within a frame and outside rendering.");

            const VulkanBuffer* l_Buffer = m_Device.GetBuffer(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "BufferBarrier on a destroyed or invalid buffer.");
            if (l_Buffer == nullptr)
            {
                return;
            }

            const VulkanState l_Before = ToVulkanState(before);
            const VulkanState l_After = ToVulkanState(after);

            VkBufferMemoryBarrier2 l_Barrier = MakeInfo<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
            l_Barrier.srcStageMask = l_Before.Stage;
            l_Barrier.srcAccessMask = l_Before.Access;
            l_Barrier.dstStageMask = l_After.Stage;
            l_Barrier.dstAccessMask = l_After.Access;
            l_Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_Barrier.buffer = l_Buffer->Buffer;
            l_Barrier.offset = 0;
            l_Barrier.size = VK_WHOLE_SIZE;
            if (HandOff(l_Barrier, before, after, m_BufferHandoffs))
            {
                return;
            }

            VkDependencyInfo l_Dependency = MakeInfo<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
            l_Dependency.bufferMemoryBarrierCount = 1;
            l_Dependency.pBufferMemoryBarriers = &l_Barrier;
            vkCmdPipelineBarrier2(m_CommandBuffer, &l_Dependency);
        }

        void VulkanCommandList::BeginRendering(const RenderingDescription& description)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "BeginRendering is called once within a frame, before EndRendering.");
            TR_CORE_ASSERT(!m_Uploads, "An upload list only copies.");
            TR_CORE_ASSERT(description.ColorAttachments.size() <= c_MaxColorAttachments, "Too many color attachments.");

            std::array<VkRenderingAttachmentInfo, c_MaxColorAttachments> l_Colors{};
            std::uint32_t l_ColorCount = 0;
            Rect l_Area = description.RenderArea;
            for (const ColorAttachment& it_Attachment : description.ColorAttachments)
            {
                const VulkanTexture* l_Texture = m_Device.GetTexture(it_Attachment.Texture);
                const VkImageView l_View = l_Texture != nullptr ? GetAttachmentView(*l_Texture, it_Attachment.MipLevel, it_Attachment.ArrayLayer) : VK_NULL_HANDLE;
                TR_CORE_ASSERT(l_View != VK_NULL_HANDLE, "BeginRendering with a destroyed texture, one without RenderTarget usage, or a mip or layer it lacks.");
                if (l_View == VK_NULL_HANDLE || l_ColorCount == c_MaxColorAttachments)
                {
                    continue;
                }

                if (l_Area.Width == 0 || l_Area.Height == 0)
                {
                    l_Area = { 0, 0, GetMipSize(l_Texture->Width, it_Attachment.MipLevel), GetMipSize(l_Texture->Height, it_Attachment.MipLevel) };
                }

                VkRenderingAttachmentInfo& l_Info = l_Colors[l_ColorCount++];
                l_Info = MakeInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
                l_Info.imageView = l_View;
                l_Info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                if (it_Attachment.ResolveTexture)
                {
                    const VulkanTexture* l_Resolve = m_Device.GetTexture(it_Attachment.ResolveTexture);
                    const VkImageView l_ResolveView = l_Resolve != nullptr ? GetAttachmentView(*l_Resolve, 0, 0) : VK_NULL_HANDLE;
                    TR_CORE_ASSERT(l_ResolveView != VK_NULL_HANDLE && l_Texture->SampleCount > 1 && l_Resolve->SampleCount == 1 && l_Resolve->TextureFormat == l_Texture->TextureFormat && l_Resolve->TextureFormat != Format::R32Uint, "BeginRendering resolving into a destroyed texture, one without RenderTarget usage, or one whose format or sample count does not fit.");
                    if (l_ResolveView != VK_NULL_HANDLE)
                    {
                        l_Info.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                        l_Info.resolveImageView = l_ResolveView;
                        l_Info.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    }
                }

                l_Info.loadOp = ToVkLoadOp(it_Attachment.Load);
                l_Info.storeOp = ToVkStoreOp(it_Attachment.Store);
                for (std::size_t it_Channel = 0; it_Channel < it_Attachment.ClearColor.size(); ++it_Channel)
                {
                    l_Info.clearValue.color.float32[it_Channel] = it_Attachment.ClearColor[it_Channel];
                }
            }

            VkRenderingAttachmentInfo l_Depth = MakeInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
            const VulkanTexture* l_DepthTexture = description.Depth.Texture ? m_Device.GetTexture(description.Depth.Texture) : nullptr;
            const VkImageView l_DepthView = l_DepthTexture != nullptr ? GetAttachmentView(*l_DepthTexture, description.Depth.MipLevel, description.Depth.ArrayLayer) : VK_NULL_HANDLE;
            TR_CORE_ASSERT(!description.Depth.Texture || l_DepthView != VK_NULL_HANDLE, "BeginRendering with a destroyed depth texture, one without DepthStencil usage, or a mip or layer it lacks.");
            if (l_DepthView != VK_NULL_HANDLE)
            {
                if (l_Area.Width == 0 || l_Area.Height == 0)
                {
                    l_Area = { 0, 0, GetMipSize(l_DepthTexture->Width, description.Depth.MipLevel), GetMipSize(l_DepthTexture->Height, description.Depth.MipLevel) };
                }

                l_Depth.imageView = l_DepthView;
                l_Depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                l_Depth.loadOp = ToVkLoadOp(description.Depth.Load);
                l_Depth.storeOp = ToVkStoreOp(description.Depth.Store);
                l_Depth.clearValue.depthStencil = { description.Depth.ClearDepth, description.Depth.ClearStencil };
            }

            VkRenderingInfo l_Rendering = MakeInfo<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
            l_Rendering.renderArea = { { l_Area.X, l_Area.Y },{ l_Area.Width, l_Area.Height } };
            l_Rendering.layerCount = 1;
            l_Rendering.colorAttachmentCount = l_ColorCount;
            l_Rendering.pColorAttachments = l_Colors.data();
            l_Rendering.pDepthAttachment = l_DepthView != VK_NULL_HANDLE ? &l_Depth : nullptr;
            l_Rendering.pStencilAttachment = l_DepthView != VK_NULL_HANDLE && HasStencil(l_DepthTexture->TextureFormat) ? &l_Depth : nullptr;
            vkCmdBeginRendering(m_CommandBuffer, &l_Rendering);

            m_Rendering = true;
            m_HasComputePipeline = false;
        }

        void VulkanCommandList::EndRendering()
        {
            TR_CORE_ASSERT(m_Rendering, "EndRendering without BeginRendering.");

            vkCmdEndRendering(m_CommandBuffer);
            m_Rendering = false;
            m_HasPipeline = false;
            m_HasIndexBuffer = false;
        }

        // Vulkan keeps a compute pipeline bound through rendering, but D3D12 does not, so both backends ask for it to be set again after rendering
        void VulkanCommandList::SetPipeline(PipelineHandle pipeline)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "SetPipeline is recorded within a frame.");
            TR_CORE_ASSERT(!m_Uploads, "An upload list only copies.");

            const VulkanPipeline* l_Pipeline = m_Device.GetPipeline(pipeline);
            TR_CORE_ASSERT(l_Pipeline != nullptr, "SetPipeline with a destroyed or invalid pipeline.");
            if (l_Pipeline == nullptr)
            {
                return;
            }

            const bool l_Compute = l_Pipeline->BindPoint == VK_PIPELINE_BIND_POINT_COMPUTE;
            TR_CORE_ASSERT(l_Compute != m_Rendering, "A graphics pipeline is set inside rendering, and a compute pipeline outside it.");

            vkCmdBindPipeline(m_CommandBuffer, l_Pipeline->BindPoint, l_Pipeline->Pipeline);
            if (l_Compute)
            {
                m_HasComputePipeline = true;
            }
            else
            {
                m_HasPipeline = true;
            }
        }

        // A negative height turns Vulkan's downward Y around, so clip space has Y up and the same winding as on D3D12
        void VulkanCommandList::SetViewport(const Viewport& viewport)
        {
            TR_CORE_ASSERT(m_Rendering, "SetViewport is recorded inside rendering.");

            const VkViewport l_Viewport{ viewport.X, viewport.Y + viewport.Height, viewport.Width, -viewport.Height, viewport.MinDepth, viewport.MaxDepth };
            vkCmdSetViewport(m_CommandBuffer, 0, 1, &l_Viewport);
        }

        void VulkanCommandList::SetScissor(const Rect& scissor)
        {
            TR_CORE_ASSERT(m_Rendering, "SetScissor is recorded inside rendering.");

            const VkRect2D l_Scissor{ { scissor.X, scissor.Y },{ scissor.Width, scissor.Height } };
            vkCmdSetScissor(m_CommandBuffer, 0, 1, &l_Scissor);
        }

        void VulkanCommandList::SetStencilReference(std::uint32_t reference)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "SetStencilReference is recorded within a frame.");

            vkCmdSetStencilReference(m_CommandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
        }

        // Push constants belong to the shared pipeline layout rather than a bind point, so graphics and compute set them the same way
        void VulkanCommandList::PushConstants(std::span<const std::byte> data)
        {
            TR_CORE_ASSERT(m_Rendering ? m_HasPipeline : m_HasComputePipeline, "PushConstants needs a graphics pipeline inside rendering, or a compute pipeline outside it.");
            TR_CORE_ASSERT(data.size() <= c_MaxPushConstantSize && data.size() % 4 == 0, "Push constants are whole 32-bit values, at most c_MaxPushConstantSize bytes.");

            vkCmdPushConstants(m_CommandBuffer, m_Device.GetPipelineLayout(), VK_SHADER_STAGE_ALL, 0, static_cast<std::uint32_t>(data.size()), data.data());
        }

        void VulkanCommandList::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance)
        {
            TR_CORE_ASSERT(m_HasPipeline, "Draw needs a pipeline.");

            vkCmdDraw(m_CommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
        }

        void VulkanCommandList::SetIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format)
        {
            TR_CORE_ASSERT(m_Rendering, "SetIndexBuffer is recorded inside rendering.");

            const VulkanBuffer* l_Buffer = m_Device.GetBuffer(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "SetIndexBuffer with a destroyed or invalid buffer.");
            TR_CORE_ASSERT(l_Buffer == nullptr || (offset < l_Buffer->Size && offset % GetIndexSize(format) == 0), "SetIndexBuffer at offset {}, which is past the end of the buffer or not a multiple of the index size.", offset);
            if (l_Buffer == nullptr || offset >= l_Buffer->Size)
            {
                return;
            }

            vkCmdBindIndexBuffer(m_CommandBuffer, l_Buffer->Buffer, offset, format == IndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            m_HasIndexBuffer = true;
        }

        void VulkanCommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::uint32_t firstInstance)
        {
            TR_CORE_ASSERT(m_HasPipeline && m_HasIndexBuffer, "DrawIndexed needs a pipeline and an index buffer.");

            vkCmdDrawIndexed(m_CommandBuffer, indexCount, instanceCount, firstIndex, 0, firstInstance);
        }

        void VulkanCommandList::Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering && m_HasComputePipeline, "Dispatch is recorded outside rendering, with a compute pipeline set.");

            vkCmdDispatch(m_CommandBuffer, groupCountX, groupCountY, groupCountZ);
        }

        void VulkanCommandList::WriteTimestamp(QueryPoolHandle pool, std::uint32_t index)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "WriteTimestamp is recorded within a frame.");

            const VulkanQueryPool* l_Pool = m_Device.GetQueryPool(pool);
            TR_CORE_ASSERT(l_Pool != nullptr && index < l_Pool->Count, "WriteTimestamp with a destroyed query pool, or an index past its end.");
            if (l_Pool == nullptr || index >= l_Pool->Count)
            {
                return;
            }

            vkCmdWriteTimestamp2(m_CommandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, l_Pool->Pool, index);
        }

        // Resetting the range after the copy readies it for the next frame. A reset is ordered after earlier query commands on the same queries, the copy included, without a barrier
        void VulkanCommandList::ResolveTimestamps(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count, BufferHandle destination, std::uint64_t destinationOffset)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "ResolveTimestamps is recorded within a frame and outside rendering.");

            const VulkanQueryPool* l_Pool = m_Device.GetQueryPool(pool);
            const VulkanBuffer* l_Buffer = m_Device.GetBuffer(destination);
            TR_CORE_ASSERT(l_Pool != nullptr && l_Buffer != nullptr, "ResolveTimestamps with a destroyed query pool or buffer.");
            if (l_Pool == nullptr || l_Buffer == nullptr)
            {
                return;
            }

            TR_CORE_ASSERT(count != 0 && std::uint64_t{ first } + count <= l_Pool->Count, "ResolveTimestamps with a range past the end of a pool of {}.", l_Pool->Count);
            TR_CORE_ASSERT(destinationOffset % 8 == 0 && destinationOffset + std::uint64_t{ count } * 8 <= l_Buffer->Size, "ResolveTimestamps at an offset that is not a multiple of 8, or past the end of the buffer.");

            vkCmdCopyQueryPoolResults(m_CommandBuffer, l_Pool->Pool, first, count, l_Buffer->Buffer, destinationOffset, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            vkCmdResetQueryPool(m_CommandBuffer, l_Pool->Pool, first, count);
        }

        void VulkanCommandList::CopyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination, std::uint64_t destinationOffset, std::uint64_t size)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "Copies are recorded within a frame and outside rendering.");
            m_HasCommands = true;

            const VulkanBuffer* l_Source = m_Device.GetBuffer(source);
            const VulkanBuffer* l_Destination = m_Device.GetBuffer(destination);
            TR_CORE_ASSERT(l_Source != nullptr && l_Destination != nullptr, "CopyBuffer with a destroyed or invalid buffer.");
            if (l_Source == nullptr || l_Destination == nullptr)
            {
                return;
            }

            TR_CORE_ASSERT(sourceOffset + size <= l_Source->Size && destinationOffset + size <= l_Destination->Size, "CopyBuffer reaches past the end of a buffer.");

            const VkBufferCopy l_Region{ sourceOffset, destinationOffset, size };
            vkCmdCopyBuffer(m_CommandBuffer, l_Source->Buffer, l_Destination->Buffer, 1, &l_Region);
        }

        // The whole mip, laid out in the buffer as CopyBufferToTexture reads it: rows of texels or blocks GetTextureCopyRowPitch apart
        void VulkanCommandList::CopyTextureToBuffer(TextureHandle source, std::uint32_t mipLevel, std::uint32_t arrayLayer, BufferHandle destination, std::uint64_t destinationOffset)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "Copies are recorded within a frame and outside rendering.");
            m_HasCommands = true;

            const VulkanTexture* l_Texture = m_Device.GetTexture(source);
            const VulkanBuffer* l_Buffer = m_Device.GetBuffer(destination);
            TR_CORE_ASSERT(l_Texture != nullptr && l_Buffer != nullptr, "CopyTextureToBuffer with a destroyed or invalid resource.");
            if (l_Texture == nullptr || l_Buffer == nullptr)
            {
                return;
            }

            const std::uint32_t l_Width = GetMipSize(l_Texture->Width, mipLevel);
            const std::uint32_t l_Height = GetMipSize(l_Texture->Height, mipLevel);
            const std::uint32_t l_BlockBytes = GetFormatSize(l_Texture->TextureFormat);
            const std::uint64_t l_RowPitch = GetTextureCopyRowPitch(l_Texture->TextureFormat, l_Width);
            [[maybe_unused]] const std::uint64_t l_Size = GetTextureCopySize(l_Texture->TextureFormat, l_Width, l_Height);
            TR_CORE_ASSERT(l_BlockBytes != 0 && l_RowPitch % l_BlockBytes == 0, "{} rows cannot be copied {} bytes apart.", ToString(l_Texture->TextureFormat), l_RowPitch);
            TR_CORE_ASSERT(mipLevel < l_Texture->MipLevels && arrayLayer < l_Texture->ArrayLayers, "CopyTextureToBuffer from mip {} of layer {} of a texture with {} mip(s) and {} layer(s).", mipLevel, arrayLayer, l_Texture->MipLevels, l_Texture->ArrayLayers);
            TR_CORE_ASSERT(destinationOffset % c_TextureCopyOffsetAlignment == 0, "CopyTextureToBuffer writes to offset {}, which is not a multiple of {}.", destinationOffset, c_TextureCopyOffsetAlignment);
            TR_CORE_ASSERT(destinationOffset + l_Size <= l_Buffer->Size, "CopyTextureToBuffer needs {} bytes from offset {}, and the buffer has {}.", l_Size, destinationOffset, l_Buffer->Size);
            if (l_BlockBytes == 0)
            {
                return;
            }

            // Vulkan counts the buffer's row length in texels, whole blocks of them for a compressed format
            VkBufferImageCopy l_Region{};
            l_Region.bufferOffset = destinationOffset;
            l_Region.bufferRowLength = static_cast<std::uint32_t>(l_RowPitch / l_BlockBytes) * GetFormatBlockDimension(l_Texture->TextureFormat);
            l_Region.imageSubresource = { GetAspect(l_Texture->TextureFormat), mipLevel, arrayLayer, 1 };
            l_Region.imageExtent = { l_Width, l_Height, 1 };
            vkCmdCopyImageToBuffer(m_CommandBuffer, l_Texture->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, l_Buffer->Buffer, 1, &l_Region);
        }

        void VulkanCommandList::CopyBufferToTexture(BufferHandle source, std::uint64_t sourceOffset, TextureHandle destination, std::uint32_t mipLevel, std::uint32_t arrayLayer, const Rect& region)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "Copies are recorded within a frame and outside rendering.");
            m_HasCommands = true;

            const VulkanBuffer* l_Buffer = m_Device.GetBuffer(source);
            const VulkanTexture* l_Texture = m_Device.GetTexture(destination);
            TR_CORE_ASSERT(l_Buffer != nullptr && l_Texture != nullptr, "CopyBufferToTexture with a destroyed or invalid resource.");
            if (l_Buffer == nullptr || l_Texture == nullptr)
            {
                return;
            }

            const std::uint32_t l_BlockBytes = GetFormatSize(l_Texture->TextureFormat);
            const std::uint64_t l_RowPitch = GetTextureCopyRowPitch(l_Texture->TextureFormat, region.Width);
            [[maybe_unused]] const std::uint64_t l_Size = GetTextureCopySize(l_Texture->TextureFormat, region.Width, region.Height);
            TR_CORE_ASSERT(l_BlockBytes != 0 && l_RowPitch % l_BlockBytes == 0, "{} rows cannot be copied {} bytes apart.", ToString(l_Texture->TextureFormat), l_RowPitch);
            TR_CORE_ASSERT(mipLevel < l_Texture->MipLevels && IsRegionInsideMip(region, l_Texture->Width, l_Texture->Height, mipLevel), "CopyBufferToTexture with a region outside mip {} of a {}x{} texture with {} mip(s).", mipLevel, l_Texture->Width, l_Texture->Height, l_Texture->MipLevels);
            TR_CORE_ASSERT(arrayLayer < l_Texture->ArrayLayers, "CopyBufferToTexture into layer {} of a texture with {} layer(s).", arrayLayer, l_Texture->ArrayLayers);
            TR_CORE_ASSERT(IsRegionBlockAligned(region, l_Texture->TextureFormat, l_Texture->Width, l_Texture->Height, mipLevel), "CopyBufferToTexture with a region of {} that does not start on a block or cover whole blocks.", ToString(l_Texture->TextureFormat));
            TR_CORE_ASSERT(sourceOffset % c_TextureCopyOffsetAlignment == 0, "CopyBufferToTexture reads from offset {}, which is not a multiple of {}.", sourceOffset, c_TextureCopyOffsetAlignment);
            TR_CORE_ASSERT(sourceOffset + l_Size <= l_Buffer->Size, "CopyBufferToTexture needs {} bytes from offset {}, and the buffer has {}.", l_Size, sourceOffset, l_Buffer->Size);
            if (l_BlockBytes == 0)
            {
                return;
            }

            VkBufferImageCopy l_Region{};
            l_Region.bufferOffset = sourceOffset;
            l_Region.bufferRowLength = static_cast<std::uint32_t>(l_RowPitch / l_BlockBytes) * GetFormatBlockDimension(l_Texture->TextureFormat);
            l_Region.imageSubresource = { GetAspect(l_Texture->TextureFormat), mipLevel, arrayLayer, 1 };
            l_Region.imageOffset = { region.X, region.Y, 0 };
            l_Region.imageExtent = { region.Width, region.Height, 1 };
            vkCmdCopyBufferToImage(m_CommandBuffer, l_Buffer->Buffer, l_Texture->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &l_Region);
        }

        // Without debug utils a label records nothing. The name is copied with its terminator into a buffer on the stack, cut at 255 bytes, so a label allocates nothing
        void VulkanCommandList::BeginLabel(std::string_view name)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "BeginLabel is recorded within a frame.");
            if (!m_Device.HasDebugUtils())
            {
                return;
            }

            std::array<char, 256> l_Name{};
            std::copy_n(name.data(), std::min(name.size(), l_Name.size() - 1), l_Name.data());

            VkDebugUtilsLabelEXT l_Label = MakeInfo<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);
            l_Label.pLabelName = l_Name.data();
            vkCmdBeginDebugUtilsLabelEXT(m_CommandBuffer, &l_Label);
        }

        void VulkanCommandList::EndLabel()
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "EndLabel is recorded within a frame.");
            if (m_Device.HasDebugUtils())
            {
                vkCmdEndDebugUtilsLabelEXT(m_CommandBuffer);
            }
        }
    }
}