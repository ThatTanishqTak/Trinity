#pragma once

#include <array>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;

    struct Vertex
    {
        glm::vec2 Position;
        glm::vec3 Color;

        static VkVertexInputBindingDescription GetBindingDescription();
        static std::array<VkVertexInputAttributeDescription, 2> GetAttributeDescriptions();
    };

    class VertexBuffer
    {
    public:
        VertexBuffer() = default;
        VertexBuffer(VulkanContext* context);
        ~VertexBuffer() = default;

        bool Create(const std::vector<Vertex>& vertices);
        void Destroy();

        VkBuffer GetBuffer() const { return m_Buffer; }
        uint32_t GetVertexCount() const { return m_VertexCount; }

    private:
        VulkanContext* m_Context = nullptr;
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_BufferMemory = VK_NULL_HANDLE;
        uint32_t m_VertexCount = 0;
    };
}