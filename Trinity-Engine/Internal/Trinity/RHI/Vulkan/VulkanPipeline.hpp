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

        // A graphics or compute pipeline on the bindless pipeline layout, with the bind point command lists bind it to
        struct VulkanPipeline
        {
            // Empty, with the reason logged, when a shader module or the pipeline cannot be created
            [[nodiscard]] static std::optional<VulkanPipeline> CreateGraphics(const VulkanDevice& device, const VulkanBindless& bindless, const GraphicsPipelineDescription& description);
            [[nodiscard]] static std::optional<VulkanPipeline> CreateCompute(const VulkanDevice& device, const VulkanBindless& bindless, const ComputePipelineDescription& description);
            // What destroying it leaves for the release queue
            [[nodiscard]] VulkanRelease ToRelease() const;

            VkPipeline Pipeline = VK_NULL_HANDLE;
            VkPipelineBindPoint BindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        };
    }
}