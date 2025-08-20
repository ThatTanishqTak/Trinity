#pragma once

#include <vector>
#include <optional>
#include <string>
#include <array>

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

        static VkVertexInputBindingDescription GetBindingDescription();
        static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions();
    };

    class Mesh
    {
    public:
        static VkVertexInputBindingDescription GetBindingDescription();
        static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions();

        explicit Mesh(VulkanContext* context);

        std::optional<std::string> Create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
        void Destroy();

        VkBuffer GetVertexBuffer() const { return m_VertexBuffer; }
        VkBuffer GetIndexBuffer() const { return m_IndexBuffer; }
        uint32_t GetIndexCount() const { return m_IndexCount; }

    private:
        VulkanContext* m_Context = nullptr;

        VkBuffer m_VertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_VertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer m_IndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_IndexBufferMemory = VK_NULL_HANDLE;
        uint32_t m_IndexCount = 0;

        std::optional<std::string> CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
        std::optional<std::string> CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        std::optional<std::string> UploadToBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory);
    };
}