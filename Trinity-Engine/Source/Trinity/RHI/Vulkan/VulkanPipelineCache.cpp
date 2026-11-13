#include "Trinity/RHI/Vulkan/VulkanPipelineCache.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            // The feedback ahead of whatever the create info chains already
            template<typename CreateInfo>
            CreateInfo WithFeedback(const CreateInfo& create, VkPipelineCreationFeedbackCreateInfo& feedbackInfo, VkPipelineCreationFeedback& feedback)
            {
                feedbackInfo = MakeInfo<VkPipelineCreationFeedbackCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO);
                feedbackInfo.pNext = create.pNext;
                feedbackInfo.pPipelineCreationFeedback = &feedback;

                CreateInfo l_Create = create;
                l_Create.pNext = &feedbackInfo;

                return l_Create;
            }

            bool IsCacheHit(const VkPipelineCreationFeedback& feedback)
            {
                return (feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT) != 0 && (feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT) != 0;
            }
        }

        VulkanPipelineCache::VulkanPipelineCache(const VulkanDevice& device) : m_Device(device)
        {

        }

        VulkanPipelineCache::~VulkanPipelineCache()
        {
            Destroy();
        }

        // The driver checks its own header in the data too, and drops data another device or driver made without a word, so the file's header is what says why
        bool VulkanPipelineCache::Initialize(std::string_view path, std::string& error)
        {
            VkPhysicalDeviceProperties l_Properties{};
            vkGetPhysicalDeviceProperties(m_Device.GetPhysicalDevice(), &l_Properties);

            PipelineCacheKey l_Key;
            l_Key.API = GraphicsAPI::Vulkan;
            l_Key.VendorID = l_Properties.vendorID;
            l_Key.DeviceID = l_Properties.deviceID;
            l_Key.DriverVersion = l_Properties.driverVersion;
            std::ranges::copy(l_Properties.pipelineCacheUUID, l_Key.DriverCacheID.begin());
            m_File.Open(path, l_Key);

            const std::span<const std::byte> l_Data = m_File.GetData();
            VkPipelineCacheCreateInfo l_Create = MakeInfo<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
            l_Create.initialDataSize = l_Data.size();
            l_Create.pInitialData = l_Data.data();

            VkResult l_Result = vkCreatePipelineCache(m_Device.GetHandle(), &l_Create, nullptr, &m_Cache);
            if (l_Result != VK_SUCCESS && !l_Data.empty())
            {
                TR_CORE_WARN("Vulkan: the driver refused the pipeline cache '{}' ({}), so a new one is started", m_File.GetPath(), FormatResult(l_Result));

                l_Create.initialDataSize = 0;
                l_Create.pInitialData = nullptr;
                l_Result = vkCreatePipelineCache(m_Device.GetHandle(), &l_Create, nullptr, &m_Cache);
            }

            if (l_Result != VK_SUCCESS)
            {
                error = std::format("the pipeline cache could not be created on {} ({})", m_Device.GetInfo().AdapterName, FormatResult(l_Result));

                return false;
            }

            m_Device.SetDebugName(VK_OBJECT_TYPE_PIPELINE_CACHE, reinterpret_cast<std::uint64_t>(m_Cache), "Pipeline cache");

            return true;
        }

        void VulkanPipelineCache::Destroy()
        {
            if (m_Cache == VK_NULL_HANDLE)
            {
                return;
            }

            std::vector<std::byte> l_Data;
            if (!m_File.GetPath().empty() && m_File.HasNewPipelines())
            {
                std::size_t l_Size = 0;
                VkResult l_Result = vkGetPipelineCacheData(m_Device.GetHandle(), m_Cache, &l_Size, nullptr);
                if (l_Result == VK_SUCCESS)
                {
                    l_Data.resize(l_Size);
                    l_Result = vkGetPipelineCacheData(m_Device.GetHandle(), m_Cache, &l_Size, l_Data.data());
                }

                if (l_Result != VK_SUCCESS)
                {
                    TR_CORE_WARN("Vulkan: the pipeline cache's data could not be read ({})", FormatResult(l_Result));
                    l_Data.clear();
                }
            }

            m_File.Close(l_Data);

            vkDestroyPipelineCache(m_Device.GetHandle(), m_Cache, nullptr);
            m_Cache = VK_NULL_HANDLE;
        }

        VkResult VulkanPipelineCache::CreateGraphics(const VkGraphicsPipelineCreateInfo& create, VkPipeline& pipeline)
        {
            VkPipelineCreationFeedback l_Feedback{};
            VkPipelineCreationFeedbackCreateInfo l_FeedbackInfo{};
            const VkGraphicsPipelineCreateInfo l_Create = WithFeedback(create, l_FeedbackInfo, l_Feedback);

            const std::chrono::steady_clock::time_point l_Start = std::chrono::steady_clock::now();
            const VkResult l_Result = vkCreateGraphicsPipelines(m_Device.GetHandle(), m_Cache, 1, &l_Create, nullptr, &pipeline);
            if (l_Result == VK_SUCCESS)
            {
                m_File.Record(std::chrono::steady_clock::now() - l_Start, IsCacheHit(l_Feedback));
            }

            return l_Result;
        }

        VkResult VulkanPipelineCache::CreateCompute(const VkComputePipelineCreateInfo& create, VkPipeline& pipeline)
        {
            VkPipelineCreationFeedback l_Feedback{};
            VkPipelineCreationFeedbackCreateInfo l_FeedbackInfo{};
            const VkComputePipelineCreateInfo l_Create = WithFeedback(create, l_FeedbackInfo, l_Feedback);

            const std::chrono::steady_clock::time_point l_Start = std::chrono::steady_clock::now();
            const VkResult l_Result = vkCreateComputePipelines(m_Device.GetHandle(), m_Cache, 1, &l_Create, nullptr, &pipeline);
            if (l_Result == VK_SUCCESS)
            {
                m_File.Record(std::chrono::steady_clock::now() - l_Start, IsCacheHit(l_Feedback));
            }

            return l_Result;
        }
    }
}