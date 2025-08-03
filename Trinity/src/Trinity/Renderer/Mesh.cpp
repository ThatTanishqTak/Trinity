#include "Trinity/trpch.h"

#include "Trinity/Renderer/Mesh.h"
#include "Trinity/Vulkan/VulkanContext.h"

namespace Trinity
{
    Mesh::Mesh(VulkanContext* context) : m_Context(context), m_VertexBuffer(context), m_IndexBuffer(context)
    {

    }

    Mesh::~Mesh()
    {
        Destroy();
    }

    bool Mesh::Create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    {
        if (!m_VertexBuffer.Create(vertices))
        {
            return false;
        }

        if (!indices.empty())
        {
            if (!m_IndexBuffer.Create(indices))
            {
                m_VertexBuffer.Destroy();

                return false;
            }
        }

        return true;
    }

    void Mesh::Destroy()
    {
        m_IndexBuffer.Destroy();
        m_VertexBuffer.Destroy();
    }
}