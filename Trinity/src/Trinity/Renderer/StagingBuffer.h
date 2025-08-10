#pragma once

#include <optional>

#include <vulkan/vulkan.h>

#include "Trinity/Renderer/BufferBase.h"

namespace Trinity
{
    class VulkanContext;

    class StagingBuffer : public BufferBase
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
        VkDeviceSize GetSize() const { return m_Count; }

    private:
        VulkanContext* m_Context = nullptr;
        void* m_Mapped = nullptr;
    };
}