#pragma once

#include <optional>
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
        ~IndexBuffer();

        IndexBuffer(const IndexBuffer&) = delete;
        IndexBuffer& operator=(const IndexBuffer&) = delete;
        IndexBuffer(IndexBuffer&& other) noexcept;
        IndexBuffer& operator=(IndexBuffer&& other) noexcept;

        std::optional<std::string> Create(const std::vector<uint32_t>& indices);
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