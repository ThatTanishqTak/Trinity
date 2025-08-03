#include "Trinity/trpch.h"

#include "Texture.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Renderer/StagingBuffer.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    Texture::Texture(VulkanContext* context) : m_Context(context)
    {

    }

    bool Texture::LoadFromFile(const std::filesystem::path& path, int width, int height)
    {
        std::vector<std::byte> l_Pixels = Utilities::FileManagement::LoadTexture(path, width, height);
        if (l_Pixels.empty())
        {
            TR_CORE_ERROR("Failed to load texture: {}", path.string());

            return false;
        }

        VkDeviceSize l_ImageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;

        StagingBuffer l_Staging(m_Context);
        if (!l_Staging.Create(l_ImageSize))
        {
            return false;
        }

        void* l_Data = l_Staging.Map();
        memcpy(l_Data, l_Pixels.data(), static_cast<size_t>(l_ImageSize));
        l_Staging.Unmap();

        VkImageCreateInfo l_ImageInfo{};
        l_ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
        l_ImageInfo.extent.width = static_cast<uint32_t>(width);
        l_ImageInfo.extent.height = static_cast<uint32_t>(height);
        l_ImageInfo.extent.depth = 1;
        l_ImageInfo.mipLevels = 1;
        l_ImageInfo.arrayLayers = 1;
        l_ImageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(m_Context->GetDevice(), &l_ImageInfo, nullptr, &m_Image) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create texture image");
            l_Staging.Destroy();

            return false;
        }

        VkMemoryRequirements l_MemReq{};
        vkGetImageMemoryRequirements(m_Context->GetDevice(), m_Image, &l_MemReq);

        VkMemoryAllocateInfo l_AllocInfo{};
        l_AllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        l_AllocInfo.allocationSize = l_MemReq.size;
        l_AllocInfo.memoryTypeIndex = m_Context->FindMemoryType(l_MemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_Context->GetDevice(), &l_AllocInfo, nullptr, &m_ImageMemory) != VK_SUCCESS)
        {
            
            TR_CORE_ERROR("Failed to allocate texture memory");
            vkDestroyImage(m_Context->GetDevice(), m_Image, nullptr);
            m_Image = VK_NULL_HANDLE;
            l_Staging.Destroy();

            return false;
        }

        vkBindImageMemory(m_Context->GetDevice(), m_Image, m_ImageMemory, 0);

        QueueFamilyIndices l_Indices = m_Context->FindQueueFamilies(m_Context->GetPhysicalDivice());

        VkCommandPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        l_PoolInfo.queueFamilyIndex = l_Indices.GraphicsFamily.value();
        l_PoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        VkCommandPool l_CommandPool = VK_NULL_HANDLE;
        vkCreateCommandPool(m_Context->GetDevice(), &l_PoolInfo, nullptr, &l_CommandPool);

        VkCommandBufferAllocateInfo l_Alloc{};
        l_Alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_Alloc.commandPool = l_CommandPool;
        l_Alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_Alloc.commandBufferCount = 1;

        VkCommandBuffer l_CommandBuffer = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_Context->GetDevice(), &l_Alloc, &l_CommandBuffer);

        VkCommandBufferBeginInfo l_BeginInfo{};
        l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        l_BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);

        VkImageMemoryBarrier l_Barrier{};
        l_Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        l_Barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_Barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_Barrier.image = m_Image;
        l_Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_Barrier.subresourceRange.baseMipLevel = 0;
        l_Barrier.subresourceRange.levelCount = 1;
        l_Barrier.subresourceRange.baseArrayLayer = 0;
        l_Barrier.subresourceRange.layerCount = 1;
        l_Barrier.srcAccessMask = 0;
        l_Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_Barrier);

        VkBufferImageCopy l_Region{};
        l_Region.bufferOffset = 0;
        l_Region.bufferRowLength = 0;
        l_Region.bufferImageHeight = 0;
        l_Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_Region.imageSubresource.mipLevel = 0;
        l_Region.imageSubresource.baseArrayLayer = 0;
        l_Region.imageSubresource.layerCount = 1;
        l_Region.imageOffset = { 0, 0, 0 };
        l_Region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };

        vkCmdCopyBufferToImage(l_CommandBuffer, l_Staging.GetBuffer(), m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &l_Region);

        l_Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_Barrier);

        vkEndCommandBuffer(l_CommandBuffer);

        VkSubmitInfo l_Submit{};
        l_Submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        l_Submit.commandBufferCount = 1;
        l_Submit.pCommandBuffers = &l_CommandBuffer;

        vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &l_Submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GetGraphicsQueue());

        vkFreeCommandBuffers(m_Context->GetDevice(), l_CommandPool, 1, &l_CommandBuffer);
        vkDestroyCommandPool(m_Context->GetDevice(), l_CommandPool, nullptr);

        l_Staging.Destroy();

        VkImageViewCreateInfo l_ViewInfo{};
        l_ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        l_ViewInfo.image = m_Image;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_ViewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = 1;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Context->GetDevice(), &l_ViewInfo, nullptr, &m_ImageView) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create texture image view");

            return false;
        }

        VkSamplerCreateInfo l_SamplerInfo{};
        l_SamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        l_SamplerInfo.magFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.minFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        l_SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        l_SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        l_SamplerInfo.anisotropyEnable = VK_FALSE;
        l_SamplerInfo.maxAnisotropy = 1.0f;
        l_SamplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        l_SamplerInfo.unnormalizedCoordinates = VK_FALSE;
        l_SamplerInfo.compareEnable = VK_FALSE;
        l_SamplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        if (vkCreateSampler(m_Context->GetDevice(), &l_SamplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create texture sampler");

            return false;
        }

        return true;
    }

    void Texture::Destroy()
    {
        if (m_Sampler)
        {
            vkDestroySampler(m_Context->GetDevice(), m_Sampler, nullptr);
            m_Sampler = VK_NULL_HANDLE;
        }

        if (m_ImageView)
        {
            vkDestroyImageView(m_Context->GetDevice(), m_ImageView, nullptr);
            m_ImageView = VK_NULL_HANDLE;
        }

        if (m_Image)
        {
            vkDestroyImage(m_Context->GetDevice(), m_Image, nullptr);
            m_Image = VK_NULL_HANDLE;
        }

        if (m_ImageMemory)
        {
            vkFreeMemory(m_Context->GetDevice(), m_ImageMemory, nullptr);
            m_ImageMemory = VK_NULL_HANDLE;
        }
    }
}