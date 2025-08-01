#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace Trinity
{
    class VulkanContext;

    struct UniformBufferObject
    {
        glm::mat4 Model;
        glm::mat4 View;
        glm::mat4 Proj;
    };

    class UniformBuffer
    {
    public:
        UniformBuffer() = default;
        UniformBuffer(VulkanContext* context);
        ~UniformBuffer() = default;

        bool Create(VkDeviceSize size);
        void Destroy();

        void* Map();
        void Unmap();

        VkBuffer GetBuffer() const { return m_Buffer; }

    private:
        VulkanContext* m_Context = nullptr;
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_BufferMemory = VK_NULL_HANDLE;
        VkDeviceSize m_Size = 0;
        void* m_Mapped = nullptr;
    };
}