#pragma once

#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;

    class StagingBuffer
    {
    public:
        StagingBuffer() = default;
        StagingBuffer(VulkanContext* context);
        ~StagingBuffer() = default;

        bool Create(VkDeviceSize size);
        void Destroy();

        void* Map();
        void Unmap();

        VkBuffer GetBuffer() const { return m_Buffer; }
        VkDeviceSize GetSize() const { return m_Size; }

    private:
        VulkanContext* m_Context = nullptr;
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_BufferMemory = VK_NULL_HANDLE;
        VkDeviceSize m_Size = 0;
        void* m_Mapped = nullptr;
    };
}