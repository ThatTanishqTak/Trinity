#include "Trinity/trpch.h"

#include "Trinity/Renderer/IndexBuffer.h"
#include "Trinity/Renderer/BufferUtility.h"
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

    IndexBuffer::IndexBuffer(IndexBuffer&& other) noexcept : BufferBase(std::move(other)), m_Context(other.m_Context)
    {
    }

    IndexBuffer& IndexBuffer::operator=(IndexBuffer&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();

            m_Context = other.m_Context;
            BufferBase::operator=(std::move(other));
        }

        return *this;
    }

    std::optional<std::string> IndexBuffer::Create(const std::vector<uint32_t>& indices)
    {
        m_Count = static_cast<VkDeviceSize>(indices.size());
        VkDeviceSize l_BufferSize = sizeof(uint32_t) * indices.size();

        return CreateDeviceBuffer(m_Context, l_BufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m_Buffer, m_BufferMemory, indices.data());
    }

    void IndexBuffer::Destroy()
    {
        DestroyBuffer(m_Context, m_Buffer, m_BufferMemory);

        TR_CORE_TRACE("Index buffer destroyed");
    }
}