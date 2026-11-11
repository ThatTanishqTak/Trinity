#pragma once

#include "Trinity/RHI/Bindless.hpp"
#include "Trinity/RHI/Resources.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"
#include "Trinity/RHI/Vulkan/VulkanReleaseQueue.hpp"

#include <cstdint>
#include <optional>

namespace Trinity
{
    namespace RHI
    {
        class VulkanBindless;
        class VulkanDevice;

        // A sampler with its index in the bindless sampler array
        struct VulkanSampler
        {
            // Empty, with the reason logged, when the sampler cannot be created or every sampler index is in use
            [[nodiscard]] static std::optional<VulkanSampler> Create(const VulkanDevice& device, VulkanBindless& bindless, const SamplerDescription& description);
            // What destroying it leaves for the release queue
            [[nodiscard]] VulkanRelease ToRelease() const;

            VkSampler Sampler = VK_NULL_HANDLE;
            std::uint32_t Index = c_NoBindlessIndex;
        };
    }
}