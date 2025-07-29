#include "Trinity/trpch.h"

#include "Trinity/Renderer/VertexBuffer.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    VertexBuffer::VertexBuffer(VulkanContext* context) : m_Context(context)
    {

    }

    VkVertexInputBindingDescription Vertex::GetBindingDescription()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        
        return binding;
    }

    std::array<VkVertexInputAttributeDescription, 2> Vertex::GetAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 2> attributes{};

        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, Position);

        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, Color);

        return attributes;
    }

    bool VertexBuffer::Create(const std::vector<Vertex>& vertices)
    {
        m_VertexCount = static_cast<uint32_t>(vertices.size());
        VkDeviceSize bufferSize = sizeof(Vertex) * vertices.size();

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_Context->GetDevice(), &bufferInfo, nullptr, &m_Buffer) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create vertex buffer");
            
            return false;
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_Context->GetDevice(), m_Buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = m_Context->FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_Context->GetDevice(), &allocInfo, nullptr, &m_BufferMemory) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to allocate vertex buffer memory");
            
            return false;
        }

        vkBindBufferMemory(m_Context->GetDevice(), m_Buffer, m_BufferMemory, 0);

        void* data = nullptr;
        vkMapMemory(m_Context->GetDevice(), m_BufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
        vkUnmapMemory(m_Context->GetDevice(), m_BufferMemory);

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