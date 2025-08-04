#include "Trinity/trpch.h"

#include "Trinity/Renderer/Mesh.h"
#include "Trinity/Vulkan/VulkanContext.h"

#include <glm/glm.hpp>

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
        m_BoundingRadius = 0.0f;
        for (const auto& l_Vertex : vertices)
        {
            m_BoundingRadius = std::max(m_BoundingRadius, glm::length(l_Vertex.Position));
        }

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