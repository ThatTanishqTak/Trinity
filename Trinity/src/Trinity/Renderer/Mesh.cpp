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

    std::optional<std::string> Mesh::Create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    {
        m_BoundingRadius = 0.0f;
        for (const auto& it_Vertex : vertices)
        {
            m_BoundingRadius = std::max(m_BoundingRadius, glm::length(it_Vertex.Position));
        }

        if (auto a_Error = m_VertexBuffer.Create(vertices))
        {
            return a_Error;
        }

        if (!indices.empty())
        {
            if (auto a_Error = m_IndexBuffer.Create(indices))
            {
                m_VertexBuffer.Destroy();
                
                return a_Error;
            }
        }

        return std::nullopt;
    }

    void Mesh::Destroy()
    {
        m_IndexBuffer.Destroy();
        m_VertexBuffer.Destroy();
    }
}