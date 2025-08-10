#include "Trinity/trpch.h"

#include "Trinity/Renderer/VertexBuffer.h"
#include "Trinity/Renderer/BufferUtility.h"
#include "Trinity/Renderer/StagingBuffer.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    VertexBuffer::VertexBuffer(VulkanContext* context) : m_Context(context)
    {

    }

    VertexBuffer::~VertexBuffer()
    {
        Destroy();
    }

    VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept : BufferBase(std::move(other)), m_Context(other.m_Context)
    {
    }

    VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();

            m_Context = other.m_Context;
            BufferBase::operator=(std::move(other));
        }

        return *this;
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
        l_Attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
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


    std::optional<std::string> VertexBuffer::Create(const std::vector<Vertex>& vertices)
    {
        m_Count = static_cast<VkDeviceSize>(vertices.size());
        VkDeviceSize l_BufferSize = sizeof(Vertex) * vertices.size();

        return CreateDeviceBuffer(m_Context, l_BufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_Buffer, m_BufferMemory, vertices.data());
    }

    void VertexBuffer::Destroy()
    {
        DestroyBuffer(m_Context, m_Buffer, m_BufferMemory);

        TR_CORE_TRACE("Vertex buffer destroyed");
    }
}