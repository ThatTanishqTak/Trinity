#pragma once

#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstdint>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;

        // One primary command buffer for each submission the frame splits into, and its timeline value
        struct VulkanCommandPool
        {
            [[nodiscard]] VkResult Create(const VulkanDevice& device, std::uint32_t queueFamily);
            // The pool keeps the command buffer until it is destroyed
            [[nodiscard]] VkResult Allocate(const VulkanDevice& device);
            // Allocated the first time a frame needs it. Null when it cannot be
            [[nodiscard]] VkCommandBuffer Next(const VulkanDevice& device);
            // Call once the frame that last recorded into it has finished on the GPU
            void Reset(const VulkanDevice& device);
            // Destroying the pool frees its command buffers
            void Destroy(const VulkanDevice& device) const;

            VkCommandPool Pool = VK_NULL_HANDLE;
            std::vector<VkCommandBuffer> CommandBuffers;
            std::uint32_t Used = 0;
            std::uint64_t CompletionValue = 0;
        };
    }
}