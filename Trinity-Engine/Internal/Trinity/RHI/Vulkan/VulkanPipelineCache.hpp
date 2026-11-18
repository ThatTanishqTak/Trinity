#pragma once

#include "Trinity/RHI/PipelineCacheFile.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <string>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;

        // Started from its file, saved at exit. VK_EXT_pipeline_creation_feedback tells what it gave
        class VulkanPipelineCache
        {
        public:
            explicit VulkanPipelineCache(const VulkanDevice& device);
            ~VulkanPipelineCache();

            VulkanPipelineCache(const VulkanPipelineCache&) = delete;
            VulkanPipelineCache& operator=(const VulkanPipelineCache&) = delete;

            // A file from another adapter, driver or engine version, or a damaged one, starts it empty
            [[nodiscard]] bool Initialize(std::string_view path, std::string& error);
            // Saves when a pipeline was added, then destroys the cache. Does nothing a second time
            void Destroy();

            // Counts the pipeline, and returns what vkCreateGraphicsPipelines or its compute twin gives
            [[nodiscard]] VkResult CreateGraphics(const VkGraphicsPipelineCreateInfo& create, VkPipeline& pipeline);
            [[nodiscard]] VkResult CreateCompute(const VkComputePipelineCreateInfo& create, VkPipeline& pipeline);

        private:
            const VulkanDevice& m_Device;

            PipelineCacheFile m_File;
            VkPipelineCache m_Cache = VK_NULL_HANDLE;
        };
    }
}