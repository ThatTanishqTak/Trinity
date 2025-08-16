#include "Trinity/trpch.h"

#include "Trinity/Renderer/Texture.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    Texture::Texture(VulkanContext* context) : m_Context(context)
    {

    }

    std::optional<std::string> Texture::CreateFromPixels(const std::vector<std::byte>& pixels, int width, int height)
    {
        if (!m_Context)
        {
            return "Invalid Vulkan context";
        }

        VkDevice l_Device = m_Context->GetDevice();
        VkDeviceSize l_ImageSize = static_cast<VkDeviceSize>(pixels.size());

        auto createBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) -> std::optional<std::string>
            {
                VkBufferCreateInfo l_BufferInfo{};
                l_BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                l_BufferInfo.size = size;
                l_BufferInfo.usage = usage;
                l_BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                if (vkCreateBuffer(l_Device, &l_BufferInfo, nullptr, &buffer) != VK_SUCCESS)
                {
                    TR_CORE_ERROR("Failed to create buffer");

                    return "Failed to create buffer";
                }

                VkMemoryRequirements l_MemReq{};
                vkGetBufferMemoryRequirements(l_Device, buffer, &l_MemReq);

                VkMemoryAllocateInfo l_AllocInfo{};
                l_AllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                l_AllocInfo.allocationSize = l_MemReq.size;
                l_AllocInfo.memoryTypeIndex = m_Context->FindMemoryType(l_MemReq.memoryTypeBits, properties);

                if (vkAllocateMemory(l_Device, &l_AllocInfo, nullptr, &memory) != VK_SUCCESS)
                {
                    TR_CORE_ERROR("Failed to allocate buffer memory");
                    vkDestroyBuffer(l_Device, buffer, nullptr);
                    buffer = VK_NULL_HANDLE;

                    return "Failed to allocate buffer memory";
                }

                vkBindBufferMemory(l_Device, buffer, memory, 0);

                return {};
            };

        VkBuffer l_StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory l_StagingBufferMemory = VK_NULL_HANDLE;
        if (auto l_Err = createBuffer(l_ImageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
            l_StagingBuffer, l_StagingBufferMemory))
        {
            return l_Err;
        }

        void* l_Data = nullptr;
        vkMapMemory(l_Device, l_StagingBufferMemory, 0, l_ImageSize, 0, &l_Data);
        std::memcpy(l_Data, pixels.data(), static_cast<size_t>(l_ImageSize));
        vkUnmapMemory(l_Device, l_StagingBufferMemory);

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
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        l_ImageInfo.flags = 0;

        if (vkCreateImage(l_Device, &l_ImageInfo, nullptr, &m_Image) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create image");
            vkDestroyBuffer(l_Device, l_StagingBuffer, nullptr);
            vkFreeMemory(l_Device, l_StagingBufferMemory, nullptr);

            return "Failed to create image";
        }

        VkMemoryRequirements l_MemReq{};
        vkGetImageMemoryRequirements(l_Device, m_Image, &l_MemReq);

        VkMemoryAllocateInfo l_AllocInfo{};
        l_AllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        l_AllocInfo.allocationSize = l_MemReq.size;
        l_AllocInfo.memoryTypeIndex = m_Context->FindMemoryType(l_MemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(l_Device, &l_AllocInfo, nullptr, &m_ImageMemory) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to allocate image memory");
            
            vkDestroyImage(l_Device, m_Image, nullptr);
            m_Image = VK_NULL_HANDLE;
            
            vkDestroyBuffer(l_Device, l_StagingBuffer, nullptr);
            vkFreeMemory(l_Device, l_StagingBufferMemory, nullptr);
            
            return "Failed to allocate image memory";
        }

        vkBindImageMemory(l_Device, m_Image, m_ImageMemory, 0);

        uint32_t l_QueueFamily = m_Context->FindQueueFamilies(m_Context->GetPhysicalDevice()).GraphicsFamily.value();

        VkCommandPool l_CommandPool = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        l_PoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        l_PoolInfo.queueFamilyIndex = l_QueueFamily;

        if (vkCreateCommandPool(l_Device, &l_PoolInfo, nullptr, &l_CommandPool) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create command pool");
            
            vkDestroyImage(l_Device, m_Image, nullptr);
            m_Image = VK_NULL_HANDLE;
            
            vkFreeMemory(l_Device, m_ImageMemory, nullptr);
            m_ImageMemory = VK_NULL_HANDLE;
            
            vkDestroyBuffer(l_Device, l_StagingBuffer, nullptr);
            vkFreeMemory(l_Device, l_StagingBufferMemory, nullptr);

            return "Failed to create command pool";
        }

        VkCommandBuffer l_CommandBuffer = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo l_AllocInfoCmd{};
        l_AllocInfoCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_AllocInfoCmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_AllocInfoCmd.commandPool = l_CommandPool;
        l_AllocInfoCmd.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(l_Device, &l_AllocInfoCmd, &l_CommandBuffer) != VK_SUCCESS)
        {
            vkDestroyCommandPool(l_Device, l_CommandPool, nullptr);
            TR_CORE_ERROR("Failed to allocate command buffer");
            
            vkDestroyImage(l_Device, m_Image, nullptr);
            m_Image = VK_NULL_HANDLE;
            
            vkFreeMemory(l_Device, m_ImageMemory, nullptr);
            m_ImageMemory = VK_NULL_HANDLE;

            vkDestroyBuffer(l_Device, l_StagingBuffer, nullptr);
            vkFreeMemory(l_Device, l_StagingBufferMemory, nullptr);

            return "Failed to allocate command buffer";
        }

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

        VkBufferImageCopy l_CopyRegion{};
        l_CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_CopyRegion.imageSubresource.mipLevel = 0;
        l_CopyRegion.imageSubresource.baseArrayLayer = 0;
        l_CopyRegion.imageSubresource.layerCount = 1;
        l_CopyRegion.imageOffset = { 0, 0, 0 };
        l_CopyRegion.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };

        vkCmdCopyBufferToImage(l_CommandBuffer, l_StagingBuffer, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &l_CopyRegion);

        l_Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_Barrier);

        vkEndCommandBuffer(l_CommandBuffer);

        VkSubmitInfo l_SubmitInfo{};
        l_SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;

        vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &l_SubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GetGraphicsQueue());

        vkFreeCommandBuffers(l_Device, l_CommandPool, 1, &l_CommandBuffer);
        vkDestroyCommandPool(l_Device, l_CommandPool, nullptr);

        vkDestroyBuffer(l_Device, l_StagingBuffer, nullptr);
        vkFreeMemory(l_Device, l_StagingBufferMemory, nullptr);

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

        if (vkCreateImageView(l_Device, &l_ViewInfo, nullptr, &m_ImageView) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create image view");
            Destroy();

            return "Failed to create image view";
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
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        l_SamplerInfo.mipLodBias = 0.0f;
        l_SamplerInfo.minLod = 0.0f;
        l_SamplerInfo.maxLod = 0.0f;

        if (vkCreateSampler(l_Device, &l_SamplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create sampler");
            Destroy();

            return "Failed to create sampler";
        }

        return {};
    }

    void Texture::Destroy()
    {
        if (!m_Context)
        {
            return;
        }

        VkDevice l_Device = m_Context->GetDevice();

        if (m_Sampler)
        {
            vkDestroySampler(l_Device, m_Sampler, nullptr);
            m_Sampler = VK_NULL_HANDLE;
        }

        if (m_ImageView)
        {
            vkDestroyImageView(l_Device, m_ImageView, nullptr);
            m_ImageView = VK_NULL_HANDLE;
        }

        if (m_Image)
        {
            vkDestroyImage(l_Device, m_Image, nullptr);
            m_Image = VK_NULL_HANDLE;
        }

        if (m_ImageMemory)
        {
            vkFreeMemory(l_Device, m_ImageMemory, nullptr);
            m_ImageMemory = VK_NULL_HANDLE;
        }
    }
}