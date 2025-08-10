#include "Trinity/trpch.h"

#include "Trinity/Renderer/StagingBuffer.h"
#include "Trinity/Renderer/BufferUtility.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    StagingBuffer::StagingBuffer(VulkanContext* context) : m_Context(context)
    {

    }

    StagingBuffer::~StagingBuffer()
    {
        Destroy();
    }

    StagingBuffer::StagingBuffer(StagingBuffer&& other) noexcept : BufferBase(std::move(other)), m_Context(other.m_Context), m_Mapped(other.m_Mapped)
    {
        other.m_Mapped = nullptr;
    }

    StagingBuffer& StagingBuffer::operator=(StagingBuffer&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();

            m_Context = other.m_Context;
            BufferBase::operator=(std::move(other));
            m_Mapped = other.m_Mapped;
            other.m_Mapped = nullptr;
        }

        return *this;
    }

    std::optional<std::string> StagingBuffer::Create(VkDeviceSize size)
    {
        m_Count = size;

        VkBufferCreateInfo l_BufferInfo{};
        l_BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        l_BufferInfo.size = size;
        l_BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        l_BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_Context->GetDevice(), &l_BufferInfo, nullptr, &m_Buffer) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create staging buffer");

            return std::string("Failed to create staging buffer");
        }

        VkMemoryRequirements l_MemoryRequirements;
        vkGetBufferMemoryRequirements(m_Context->GetDevice(), m_Buffer, &l_MemoryRequirements);

        VkMemoryAllocateInfo l_allocateInfo{};
        l_allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        l_allocateInfo.allocationSize = l_MemoryRequirements.size;
        l_allocateInfo.memoryTypeIndex = m_Context->FindMemoryType(l_MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_Context->GetDevice(), &l_allocateInfo, nullptr, &m_BufferMemory) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to allocate staging buffer memory");

            return std::string("Failed to allocate staging buffer memory");
        }

        vkBindBufferMemory(m_Context->GetDevice(), m_Buffer, m_BufferMemory, 0);

        return std::nullopt;
    }

    void StagingBuffer::Destroy()
    {
        DestroyBuffer(m_Context, m_Buffer, m_BufferMemory);

        m_Mapped = nullptr;
        m_Count = 0;

        TR_CORE_TRACE("Staging buffer destroyed");
    }

    void* StagingBuffer::Map()
    {
        if (!m_Mapped)
        {
            vkMapMemory(m_Context->GetDevice(), m_BufferMemory, 0, m_Count, 0, &m_Mapped);
        }

        return m_Mapped;
    }

    void StagingBuffer::Unmap()
    {
        if (m_Mapped)
        {
            vkUnmapMemory(m_Context->GetDevice(), m_BufferMemory);

            m_Mapped = nullptr;
        }
    }
}