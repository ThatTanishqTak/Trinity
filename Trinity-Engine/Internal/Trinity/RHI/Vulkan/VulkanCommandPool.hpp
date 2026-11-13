#pragma once

#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstdint>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;

        // A frame's transient command pool with its one primary command buffer, and the timeline value the frame signals once its commands have finished
        struct VulkanCommandPool
        {
            // VK_SUCCESS, or the result of the call that failed
            [[nodiscard]] VkResult Create(const VulkanDevice& device);
            // Once the frame that last recorded into it has finished on the GPU
            void Reset(const VulkanDevice& device) const;
            // The pool, which frees its command buffer with it
            void Destroy(const VulkanDevice& device) const;

            VkCommandPool Pool = VK_NULL_HANDLE;
            VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
            std::uint64_t CompletionValue = 0;
        };
    }
}