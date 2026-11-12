#pragma once

#include "Trinity/RHI/Resources.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"
#include "Trinity/RHI/Vulkan/VulkanReleaseQueue.hpp"

#include <cstdint>
#include <optional>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;

        // A pool of timestamps the GPU writes on the graphics queue
        struct VulkanQueryPool
        {
            // Empty, with the reason logged, when the device has no timestamps or the pool cannot be created
            [[nodiscard]] static std::optional<VulkanQueryPool> Create(const VulkanDevice& device, const QueryPoolDescription& description);
            // Ticks per second of the timestamps the device writes
            [[nodiscard]] static std::uint64_t GetTimestampFrequency(const VulkanDevice& device);
            // What destroying it leaves for the release queue
            [[nodiscard]] VulkanRelease ToRelease() const;

            VkQueryPool Pool = VK_NULL_HANDLE;
            std::uint32_t Count = 0;
        };
    }
}