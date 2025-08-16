#pragma once

#include <vector>
#include <optional>
#include <string>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace Trinity
{
    class VulkanContext;

    struct Vertex
    {
        glm::vec3 Position{};
        glm::vec3 Color{};
        glm::vec2 TexCoord{};
    };

    class Mesh
    {
    public:
        explicit Mesh(VulkanContext* context);

        std::optional<std::string> Create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
        void Destroy();

    private:
        VulkanContext* m_Context = nullptr;

        VkBuffer m_VertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_VertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer m_IndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_IndexBufferMemory = VK_NULL_HANDLE;
    };
}