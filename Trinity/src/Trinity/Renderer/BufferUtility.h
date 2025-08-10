#pragma once

#include <optional>
#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;

    std::optional<std::string> CreateDeviceBuffer(VulkanContext* context, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& bufferMemory, const void* data);
    void DestroyBuffer(VulkanContext* context, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
}