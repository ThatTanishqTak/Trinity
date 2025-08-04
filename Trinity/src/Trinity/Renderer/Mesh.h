#pragma once

#include <vector>

#include "Trinity/Renderer/VertexBuffer.h"
#include "Trinity/Renderer/IndexBuffer.h"

namespace Trinity
{
    class VulkanContext;

    class Mesh
    {
    public:
        Mesh() = default;
        explicit Mesh(VulkanContext* context);
        ~Mesh();

        bool Create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
        void Destroy();

        VertexBuffer& GetVertexBuffer() { return m_VertexBuffer; }
        IndexBuffer& GetIndexBuffer() { return m_IndexBuffer; }
        float GetBoundingRadius() const { return m_BoundingRadius; }

    private:
        VulkanContext* m_Context = nullptr;
        VertexBuffer m_VertexBuffer{};
        IndexBuffer m_IndexBuffer{};
        float m_BoundingRadius = 0.0f;
    };
}