#include "Trinity/trpch.h"

#include "Trinity/Renderer/VertexBuffer.h"
#include "Trinity/Renderer/StagingBuffer.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    VertexBuffer::VertexBuffer(VulkanContext* context) : m_Context(context)
    {

    }

    VkVertexInputBindingDescription Vertex::GetBindingDescription()
    {
        VkVertexInputBindingDescription l_Binding{};
        l_Binding.binding = 0;
        l_Binding.stride = sizeof(Vertex);
        l_Binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        
        return l_Binding;
    }

    std::array<VkVertexInputAttributeDescription, 3> Vertex::GetAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 3> l_Attributes{};

        l_Attributes[0].binding = 0;
        l_Attributes[0].location = 0;
        l_Attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        l_Attributes[0].offset = offsetof(Vertex, Position);

        l_Attributes[1].binding = 0;
        l_Attributes[1].location = 1;
        l_Attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        l_Attributes[1].offset = offsetof(Vertex, Color);

        l_Attributes[2].binding = 0;
        l_Attributes[2].location = 2;
        l_Attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        l_Attributes[2].offset = offsetof(Vertex, TexCoord);

        return l_Attributes;
    }


    bool VertexBuffer::Create(const std::vector<Vertex>& vertices)
    {
        m_VertexCount = static_cast<uint32_t>(vertices.size());
        VkDeviceSize l_BufferSize = sizeof(Vertex) * vertices.size();

        // Create staging buffer
        StagingBuffer l_Staging(m_Context);

        TR_CORE_TRACE("Creating staging buffer");

        if (!l_Staging.Create(l_BufferSize))
        {
            return false;
        }

        TR_CORE_TRACE("Staging buffer created");

        void* l_Data = l_Staging.Map();
        memcpy(l_Data, vertices.data(), static_cast<size_t>(l_BufferSize));
        l_Staging.Unmap();

        VkBufferCreateInfo l_BufferInfo{};
        l_BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        l_BufferInfo.size = l_BufferSize;
        l_BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        l_BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_Context->GetDevice(), &l_BufferInfo, nullptr, &m_Buffer) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create vertex buffer");

            l_Staging.Destroy();
            
            return false;
        }

        VkMemoryRequirements l_MemoryRequirements;
        vkGetBufferMemoryRequirements(m_Context->GetDevice(), m_Buffer, &l_MemoryRequirements);

        VkMemoryAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        l_AllocateInfo.allocationSize = l_MemoryRequirements.size;
        l_AllocateInfo.memoryTypeIndex = m_Context->FindMemoryType(l_MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_Context->GetDevice(), &l_AllocateInfo, nullptr, &m_BufferMemory) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to allocate vertex buffer memory");

            l_Staging.Destroy();
            
            return false;
        }

        vkBindBufferMemory(m_Context->GetDevice(), m_Buffer, m_BufferMemory, 0);

        // Copy from staging buffer to vertex buffer
        QueueFamilyIndices l_Indices = m_Context->FindQueueFamilies(m_Context->GetPhysicalDivice());

        VkCommandPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        l_PoolInfo.queueFamilyIndex = l_Indices.GraphicsFamily.value();

        VkCommandPool l_CommandPool = VK_NULL_HANDLE;
        vkCreateCommandPool(m_Context->GetDevice(), &l_PoolInfo, nullptr, &l_CommandPool);

        VkCommandBufferAllocateInfo l_allocateCommandBuffer{};
        l_allocateCommandBuffer.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_allocateCommandBuffer.commandPool = l_CommandPool;
        l_allocateCommandBuffer.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_allocateCommandBuffer.commandBufferCount = 1;

        VkCommandBuffer l_CommandBuffer = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_Context->GetDevice(), &l_allocateCommandBuffer, &l_CommandBuffer);

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

        return true;
    }

    void VertexBuffer::Destroy()
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

        TR_CORE_TRACE("Vertex buffer destroyed");
    }
}