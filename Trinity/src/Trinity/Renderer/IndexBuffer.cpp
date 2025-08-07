#include "Trinity/trpch.h"

#include "Trinity/Renderer/IndexBuffer.h"
#include "Trinity/Renderer/StagingBuffer.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    IndexBuffer::IndexBuffer(VulkanContext* context) : m_Context(context)
    {

    }

    IndexBuffer::~IndexBuffer()
    {
        Destroy();
    }

    IndexBuffer::IndexBuffer(IndexBuffer&& other) noexcept : m_Context(other.m_Context), m_Buffer(other.m_Buffer), 
        m_BufferMemory(other.m_BufferMemory), m_IndexCount(other.m_IndexCount)
    {
        other.m_Buffer = VK_NULL_HANDLE;
        other.m_BufferMemory = VK_NULL_HANDLE;
        other.m_IndexCount = 0;
    }

    IndexBuffer& IndexBuffer::operator=(IndexBuffer&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
        
            m_Context = other.m_Context;
            m_Buffer = other.m_Buffer;
            m_BufferMemory = other.m_BufferMemory;
            m_IndexCount = other.m_IndexCount;
            other.m_Buffer = VK_NULL_HANDLE;
            other.m_BufferMemory = VK_NULL_HANDLE;
            other.m_IndexCount = 0;
        }

        return *this;
    }

    std::optional<std::string> IndexBuffer::Create(const std::vector<uint32_t>& indices)
    {
        m_IndexCount = static_cast<uint32_t>(indices.size());
        VkDeviceSize l_BufferSize = sizeof(uint32_t) * indices.size();

        StagingBuffer l_Staging(m_Context);

        TR_CORE_TRACE("Creating staging buffer for indices");

        if (auto l_Error = l_Staging.Create(l_BufferSize))
        {
            return l_Error;
        }

        TR_CORE_TRACE("Index staging buffer created");

        void* l_Data = l_Staging.Map();
        memcpy(l_Data, indices.data(), static_cast<size_t>(l_BufferSize));
        l_Staging.Unmap();

        VkBufferCreateInfo l_BufferInfo{};
        l_BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        l_BufferInfo.size = l_BufferSize;
        l_BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        l_BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_Context->GetDevice(), &l_BufferInfo, nullptr, &m_Buffer) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create index buffer");
            l_Staging.Destroy();
            return std::string("Failed to create index buffer");
        }

        VkMemoryRequirements l_MemoryRequirements;
        vkGetBufferMemoryRequirements(m_Context->GetDevice(), m_Buffer, &l_MemoryRequirements);

        VkMemoryAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        l_AllocateInfo.allocationSize = l_MemoryRequirements.size;
        l_AllocateInfo.memoryTypeIndex = m_Context->FindMemoryType(l_MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_Context->GetDevice(), &l_AllocateInfo, nullptr, &m_BufferMemory) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to allocate index buffer memory");
            l_Staging.Destroy();
            return std::string("Failed to allocate index buffer memory");
        }

        vkBindBufferMemory(m_Context->GetDevice(), m_Buffer, m_BufferMemory, 0);

        QueueFamilyIndices l_Indices = m_Context->FindQueueFamilies(m_Context->GetPhysicalDevice());

        VkCommandPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        l_PoolInfo.queueFamilyIndex = l_Indices.GraphicsFamily.value();

        VkCommandPool l_CommandPool = VK_NULL_HANDLE;
        vkCreateCommandPool(m_Context->GetDevice(), &l_PoolInfo, nullptr, &l_CommandPool);

        VkCommandBufferAllocateInfo l_AllocateBuffer{};
        l_AllocateBuffer.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_AllocateBuffer.commandPool = l_CommandPool;
        l_AllocateBuffer.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_AllocateBuffer.commandBufferCount = 1;

        VkCommandBuffer l_CommandBuffer = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_Context->GetDevice(), &l_AllocateBuffer, &l_CommandBuffer);

        VkCommandBufferBeginInfo l_BeginInfo{};
        l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        l_BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);

        VkBufferCopy l_CopyRegion{};
        l_CopyRegion.size = l_BufferSize;
        vkCmdCopyBuffer(l_CommandBuffer, l_Staging.GetBuffer(), m_Buffer, 1, &l_CopyRegion);

        vkEndCommandBuffer(l_CommandBuffer);

        VkSubmitInfo l_SubmitInfo{};
        l_SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;

        vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &l_SubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GetGraphicsQueue());

        vkFreeCommandBuffers(m_Context->GetDevice(), l_CommandPool, 1, &l_CommandBuffer);
        vkDestroyCommandPool(m_Context->GetDevice(), l_CommandPool, nullptr);

        l_Staging.Destroy();

        return std::nullopt;
    }

    void IndexBuffer::Destroy()
    {
        if (m_BufferMemory)
        {
            vkFreeMemory(m_Context->GetDevice(), m_BufferMemory, nullptr);
            m_BufferMemory = VK_NULL_HANDLE;
        }

        if (m_Buffer)
        {
            vkDestroyBuffer(m_Context->GetDevice(), m_Buffer, nullptr);
            m_Buffer = VK_NULL_HANDLE;
        }

        TR_CORE_TRACE("Index buffer destroyed");
    }
}