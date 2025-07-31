#pragma once

#include <vector>
#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;

    class IndexBuffer
    {
    public:
        IndexBuffer() = default;
        IndexBuffer(VulkanContext* context);
        ~IndexBuffer() = default;

        bool Create(const std::vector<uint32_t>& indices);
        void Destroy();

        VkBuffer GetBuffer() const { return m_Buffer; }
        uint32_t GetIndexCount() const { return m_IndexCount; }

    private:
        VulkanContext* m_Context = nullptr;
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_BufferMemory = VK_NULL_HANDLE;
        uint32_t m_IndexCount = 0;
    };
}