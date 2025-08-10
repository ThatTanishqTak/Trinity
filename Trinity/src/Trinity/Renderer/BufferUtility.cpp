#include "Trinity/trpch.h"

#include "Trinity/Renderer/BufferUtility.h"
#include "Trinity/Renderer/StagingBuffer.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    std::optional<std::string> CreateDeviceBuffer(VulkanContext* context, VkDeviceSize size, VkBufferUsageFlags usage,
        VkBuffer& buffer, VkDeviceMemory& bufferMemory, const void* data)
    {
        StagingBuffer l_Staging(context);

        TR_CORE_TRACE("Creating staging buffer");
        if (auto a_Error = l_Staging.Create(size))
        {
            return a_Error;
        }

        TR_CORE_TRACE("Staging buffer created");

        void* l_Data = l_Staging.Map();
        memcpy(l_Data, data, static_cast<size_t>(size));
        l_Staging.Unmap();

        VkBufferCreateInfo l_BufferInfo{};
        l_BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        l_BufferInfo.size = size;
        l_BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage;
        l_BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(context->GetDevice(), &l_BufferInfo, nullptr, &buffer) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create device buffer");
            l_Staging.Destroy();
            return std::string("Failed to create device buffer");
        }

        VkMemoryRequirements l_MemoryRequirements{};
        vkGetBufferMemoryRequirements(context->GetDevice(), buffer, &l_MemoryRequirements);

        VkMemoryAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        l_AllocateInfo.allocationSize = l_MemoryRequirements.size;
        l_AllocateInfo.memoryTypeIndex = context->FindMemoryType(l_MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(context->GetDevice(), &l_AllocateInfo, nullptr, &bufferMemory) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to allocate device buffer memory");
            vkDestroyBuffer(context->GetDevice(), buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            l_Staging.Destroy();
            return std::string("Failed to allocate device buffer memory");
        }

        vkBindBufferMemory(context->GetDevice(), buffer, bufferMemory, 0);

        QueueFamilyIndices l_Indices = context->FindQueueFamilies(context->GetPhysicalDevice());

        VkCommandPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        l_PoolInfo.queueFamilyIndex = l_Indices.GraphicsFamily.value();

        VkCommandPool l_CommandPool = VK_NULL_HANDLE;
        vkCreateCommandPool(context->GetDevice(), &l_PoolInfo, nullptr, &l_CommandPool);

        VkCommandBufferAllocateInfo l_AllocateCommandBuffer{};
        l_AllocateCommandBuffer.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_AllocateCommandBuffer.commandPool = l_CommandPool;
        l_AllocateCommandBuffer.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_AllocateCommandBuffer.commandBufferCount = 1;

        VkCommandBuffer l_CommandBuffer = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(context->GetDevice(), &l_AllocateCommandBuffer, &l_CommandBuffer);

        VkCommandBufferBeginInfo l_BeginInfo{};
        l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        l_BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);

        VkBufferCopy l_CopyRegion{};
        l_CopyRegion.size = size;
        vkCmdCopyBuffer(l_CommandBuffer, l_Staging.GetBuffer(), buffer, 1, &l_CopyRegion);

        vkEndCommandBuffer(l_CommandBuffer);

        VkSubmitInfo l_SubmitInfo{};
        l_SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;

        vkQueueSubmit(context->GetGraphicsQueue(), 1, &l_SubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(context->GetGraphicsQueue());

        vkFreeCommandBuffers(context->GetDevice(), l_CommandPool, 1, &l_CommandBuffer);
        vkDestroyCommandPool(context->GetDevice(), l_CommandPool, nullptr);

        l_Staging.Destroy();

        return std::nullopt;
    }

    void DestroyBuffer(VulkanContext* context, VkBuffer& buffer, VkDeviceMemory& bufferMemory)
    {
        if (bufferMemory)
        {
            vkFreeMemory(context->GetDevice(), bufferMemory, nullptr);
            bufferMemory = VK_NULL_HANDLE;
        }

        if (buffer)
        {
            vkDestroyBuffer(context->GetDevice(), buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }
    }
}