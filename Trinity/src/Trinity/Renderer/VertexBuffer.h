#pragma once

#include <array>
#include <vector>
#include <optional>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "Trinity/Renderer/BufferBase.h"

namespace Trinity
{
    class VulkanContext;

    struct Vertex
    {
        glm::vec3 Position;
        glm::vec3 Color;
        glm::vec2 TexCoord;

        static VkVertexInputBindingDescription GetBindingDescription();
        static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions();
    };

    class VertexBuffer : public BufferBase
    {
    public:
        VertexBuffer() = default;
        VertexBuffer(VulkanContext* context);
        ~VertexBuffer();

        VertexBuffer(const VertexBuffer&) = delete;
        VertexBuffer& operator=(const VertexBuffer&) = delete;
        VertexBuffer(VertexBuffer&& other) noexcept;
        VertexBuffer& operator=(VertexBuffer&& other) noexcept;

        std::optional<std::string> Create(const std::vector<Vertex>& vertices);
        void Destroy();

        VkBuffer GetBuffer() const { return m_Buffer; }
        uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_Count); }

    private:
        VulkanContext* m_Context = nullptr;
    };
}