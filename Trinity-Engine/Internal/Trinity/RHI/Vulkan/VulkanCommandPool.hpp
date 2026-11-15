#pragma once

#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstdint>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;

        // A frame's transient command pool on a queue family, with its primary command buffers, one for each submission the frame splits its commands into, and the timeline value the frame signals once its commands have finished
        struct VulkanCommandPool
        {
            // VK_SUCCESS, or the result of the call that failed
            [[nodiscard]] VkResult Create(const VulkanDevice& device, std::uint32_t queueFamily);
            // One more command buffer, which the pool keeps until it is destroyed
            [[nodiscard]] VkResult Allocate(const VulkanDevice& device);
            // The first command buffer since Reset, then another each time, allocated the first time a frame needs it. Null when it cannot be
            [[nodiscard]] VkCommandBuffer Next(const VulkanDevice& device);
            // Once the frame that last recorded into it has finished on the GPU
            void Reset(const VulkanDevice& device);
            // The pool, which frees its command buffers with it
            void Destroy(const VulkanDevice& device) const;

            VkCommandPool Pool = VK_NULL_HANDLE;
            std::vector<VkCommandBuffer> CommandBuffers;
            std::uint32_t Used = 0;
            std::uint64_t CompletionValue = 0;
        };
    }
}