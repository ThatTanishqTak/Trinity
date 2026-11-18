#pragma once

#include "Trinity/RHI/Pipeline.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"
#include "Trinity/RHI/Vulkan/VulkanReleaseQueue.hpp"

#include <optional>

namespace Trinity
{
    namespace RHI
    {
        class VulkanBindless;
        class VulkanDevice;
        class VulkanPipelineCache;

        struct VulkanPipeline
        {
            // Made through the pipeline cache. Empty, with the reason logged, on failure
            [[nodiscard]] static std::optional<VulkanPipeline> CreateGraphics(const VulkanDevice& device, const VulkanBindless& bindless, VulkanPipelineCache& cache, const GraphicsPipelineDescription& description);
            [[nodiscard]] static std::optional<VulkanPipeline> CreateCompute(const VulkanDevice& device, const VulkanBindless& bindless, VulkanPipelineCache& cache, const ComputePipelineDescription& description);
            // What destroying it leaves for the release queue
            [[nodiscard]] VulkanRelease ToRelease() const;

            VkPipeline Pipeline = VK_NULL_HANDLE;
            VkPipelineBindPoint BindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        };
    }
}