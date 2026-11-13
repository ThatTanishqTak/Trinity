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

        // The driver's pipeline cache, started from its file and saved to it at exit once a pipeline was added, which every pipeline is made through, VK_EXT_pipeline_creation_feedback telling which came from it
        class VulkanPipelineCache
        {
        public:
            explicit VulkanPipelineCache(const VulkanDevice& device);
            ~VulkanPipelineCache();

            VulkanPipelineCache(const VulkanPipelineCache&) = delete;
            VulkanPipelineCache& operator=(const VulkanPipelineCache&) = delete;

            // Starts from the file at path, or empty when there is none, it was made by another adapter, driver or engine version, or it is damaged. Without a path the cache lasts as long as the device. False, with the reason in error, when the cache cannot be created
            [[nodiscard]] bool Initialize(std::string_view path, std::string& error);
            // Saves the cache when a pipeline was added to it, then destroys it. Does nothing a second time
            void Destroy();

            // One pipeline made through the cache and counted, with the result vkCreateGraphicsPipelines or vkCreateComputePipelines gives
            [[nodiscard]] VkResult CreateGraphics(const VkGraphicsPipelineCreateInfo& create, VkPipeline& pipeline);
            [[nodiscard]] VkResult CreateCompute(const VkComputePipelineCreateInfo& create, VkPipeline& pipeline);

        private:
            const VulkanDevice& m_Device;

            PipelineCacheFile m_File;
            VkPipelineCache m_Cache = VK_NULL_HANDLE;
        };
    }
}