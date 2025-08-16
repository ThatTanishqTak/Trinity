#include "Trinity/trpch.h"

#include "Trinity/Renderer/Mesh.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    Mesh::Mesh(VulkanContext* context) : m_Context(context)
    {
    }

    std::optional<std::string> Mesh::Create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    {
        if (!m_Context)
        {
            return "Invalid Vulkan context";
        }

        VkDevice l_Device = m_Context->GetDevice();
        VkDeviceSize l_VertexBufferSize = sizeof(Vertex) * vertices.size();
        VkDeviceSize l_IndexBufferSize = sizeof(uint32_t) * indices.size();

        auto createBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
            VkBuffer& buffer, VkDeviceMemory& memory) -> std::optional<std::string>
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

        VkBuffer l_VertexStagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory l_VertexStagingMemory = VK_NULL_HANDLE;
        if (auto l_Err = createBuffer(l_VertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            l_VertexStagingBuffer, l_VertexStagingMemory))
        {
            return l_Err;
        }

        void* l_Data = nullptr;
        vkMapMemory(l_Device, l_VertexStagingMemory, 0, l_VertexBufferSize, 0, &l_Data);
        std::memcpy(l_Data, vertices.data(), static_cast<size_t>(l_VertexBufferSize));
        vkUnmapMemory(l_Device, l_VertexStagingMemory);

        VkBuffer l_IndexStagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory l_IndexStagingMemory = VK_NULL_HANDLE;
        if (auto l_Err = createBuffer(l_IndexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            l_IndexStagingBuffer, l_IndexStagingMemory))
        {
            vkDestroyBuffer(l_Device, l_VertexStagingBuffer, nullptr);
            vkFreeMemory(l_Device, l_VertexStagingMemory, nullptr);
            return l_Err;
        }

        vkMapMemory(l_Device, l_IndexStagingMemory, 0, l_IndexBufferSize, 0, &l_Data);
        std::memcpy(l_Data, indices.data(), static_cast<size_t>(l_IndexBufferSize));
        vkUnmapMemory(l_Device, l_IndexStagingMemory);

        if (auto l_Err = createBuffer(l_VertexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_VertexBuffer, m_VertexBufferMemory))
        {
            vkDestroyBuffer(l_Device, l_VertexStagingBuffer, nullptr);
            vkFreeMemory(l_Device, l_VertexStagingMemory, nullptr);
            vkDestroyBuffer(l_Device, l_IndexStagingBuffer, nullptr);
            vkFreeMemory(l_Device, l_IndexStagingMemory, nullptr);
            return l_Err;
        }

        if (auto l_Err = createBuffer(l_IndexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_IndexBuffer, m_IndexBufferMemory))
        {
            vkDestroyBuffer(l_Device, l_VertexStagingBuffer, nullptr);
            vkFreeMemory(l_Device, l_VertexStagingMemory, nullptr);
            vkDestroyBuffer(l_Device, l_IndexStagingBuffer, nullptr);
            vkFreeMemory(l_Device, l_IndexStagingMemory, nullptr);
            vkDestroyBuffer(l_Device, m_VertexBuffer, nullptr);
            vkFreeMemory(l_Device, m_VertexBufferMemory, nullptr);
            m_VertexBuffer = VK_NULL_HANDLE;
            m_VertexBufferMemory = VK_NULL_HANDLE;
            return l_Err;
        }

        uint32_t l_QueueFamily = m_Context->FindQueueFamilies(m_Context->GetPhysicalDevice()).GraphicsFamily.value();

        VkCommandPool l_CommandPool = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        l_PoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        l_PoolInfo.queueFamilyIndex = l_QueueFamily;

        if (vkCreateCommandPool(l_Device, &l_PoolInfo, nullptr, &l_CommandPool) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create command pool");
            return "Failed to create command pool";
        }

        VkCommandBuffer l_CommandBuffer = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo l_AllocInfo{};
        l_AllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_AllocInfo.commandPool = l_CommandPool;
        l_AllocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(l_Device, &l_AllocInfo, &l_CommandBuffer) != VK_SUCCESS)
        {
            vkDestroyCommandPool(l_Device, l_CommandPool, nullptr);
            TR_CORE_ERROR("Failed to allocate command buffer");
            return "Failed to allocate command buffer";
        }

        VkCommandBufferBeginInfo l_BeginInfo{};
        l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        l_BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);

        VkBufferCopy l_CopyRegion{};
        l_CopyRegion.size = l_VertexBufferSize;
        vkCmdCopyBuffer(l_CommandBuffer, l_VertexStagingBuffer, m_VertexBuffer, 1, &l_CopyRegion);
        l_CopyRegion.size = l_IndexBufferSize;
        vkCmdCopyBuffer(l_CommandBuffer, l_IndexStagingBuffer, m_IndexBuffer, 1, &l_CopyRegion);

        vkEndCommandBuffer(l_CommandBuffer);

        VkSubmitInfo l_SubmitInfo{};
        l_SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;

        vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &l_SubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GetGraphicsQueue());

        vkFreeCommandBuffers(l_Device, l_CommandPool, 1, &l_CommandBuffer);
        vkDestroyCommandPool(l_Device, l_CommandPool, nullptr);

        vkDestroyBuffer(l_Device, l_VertexStagingBuffer, nullptr);
        vkFreeMemory(l_Device, l_VertexStagingMemory, nullptr);
        vkDestroyBuffer(l_Device, l_IndexStagingBuffer, nullptr);
        vkFreeMemory(l_Device, l_IndexStagingMemory, nullptr);

        return {};
    }

    void Mesh::Destroy()
    {
        if (!m_Context)
        {
            return;
        }

        VkDevice l_Device = m_Context->GetDevice();

        if (m_VertexBuffer)
        {
            vkDestroyBuffer(l_Device, m_VertexBuffer, nullptr);
            m_VertexBuffer = VK_NULL_HANDLE;
        }

        if (m_VertexBufferMemory)
        {
            vkFreeMemory(l_Device, m_VertexBufferMemory, nullptr);
            m_VertexBufferMemory = VK_NULL_HANDLE;
        }

        if (m_IndexBuffer)
        {
            vkDestroyBuffer(l_Device, m_IndexBuffer, nullptr);
            m_IndexBuffer = VK_NULL_HANDLE;
        }

        if (m_IndexBufferMemory)
        {
            vkFreeMemory(l_Device, m_IndexBufferMemory, nullptr);
            m_IndexBufferMemory = VK_NULL_HANDLE;
        }
    }
}