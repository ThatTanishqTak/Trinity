#pragma once

#include <optional>

#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;

    class StagingBuffer
    {
    public:
        StagingBuffer() = default;
        StagingBuffer(VulkanContext* context);
        ~StagingBuffer();

        StagingBuffer(const StagingBuffer&) = delete;
        StagingBuffer& operator=(const StagingBuffer&) = delete;
        StagingBuffer(StagingBuffer&& other) noexcept;
        StagingBuffer& operator=(StagingBuffer&& other) noexcept;

        std::optional<std::string> Create(VkDeviceSize size);
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