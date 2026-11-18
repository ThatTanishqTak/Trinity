#include "Trinity/RHI/Vulkan/VulkanFrameSync.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include <cstdint>

namespace Trinity
{
    namespace RHI
    {
        VulkanFrameSync::VulkanFrameSync(const VulkanDevice& device) : m_Device(device)
        {

        }

        VkResult VulkanFrameSync::Initialize()
        {
            VkSemaphoreTypeCreateInfo l_Timeline = MakeInfo<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
            l_Timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

            VkSemaphoreCreateInfo l_Semaphore = MakeInfo<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
            l_Semaphore.pNext = &l_Timeline;

            VkResult l_Result = VK_SUCCESS;
            for (VkSemaphore* it_Timeline : { &m_Timeline, &m_UploadTimeline, &m_ComputeTimeline })
            {
                if (l_Result == VK_SUCCESS)
                {
                    l_Result = vkCreateSemaphore(m_Device.GetHandle(), &l_Semaphore, nullptr, it_Timeline);
                }
            }

            return l_Result;
        }

        void VulkanFrameSync::Destroy()
        {
            vkDestroySemaphore(m_Device.GetHandle(), m_ComputeTimeline, nullptr);
            vkDestroySemaphore(m_Device.GetHandle(), m_UploadTimeline, nullptr);
            vkDestroySemaphore(m_Device.GetHandle(), m_Timeline, nullptr);
        }

        void VulkanFrameSync::WaitForTimeline(std::uint64_t value) const
        {
            if (value != 0)
            {
                VkSemaphoreWaitInfo l_Wait = MakeInfo<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
                l_Wait.semaphoreCount = 1;
                l_Wait.pSemaphores = &m_Timeline;
                l_Wait.pValues = &value;
                vkWaitSemaphores(m_Device.GetHandle(), &l_Wait, UINT64_MAX);
            }
        }

        void VulkanFrameSync::BeginFrame()
        {
            m_Waits.clear();
            m_Signals.clear();
        }

        void VulkanFrameSync::WaitForSemaphore(VkSemaphore semaphore)
        {
            VkSemaphoreSubmitInfo l_Wait = MakeInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            l_Wait.semaphore = semaphore;
            l_Wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            m_Waits.push_back(l_Wait);
        }

        void VulkanFrameSync::SignalSemaphore(VkSemaphore semaphore)
        {
            VkSemaphoreSubmitInfo l_Signal = MakeInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            l_Signal.semaphore = semaphore;
            l_Signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            m_Signals.push_back(l_Signal);
        }

        VkResult VulkanFrameSync::Submit(VkCommandBuffer commandBuffer, bool endsFrame)
        {
            VkSemaphoreSubmitInfo l_Timeline = MakeInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            l_Timeline.semaphore = m_Timeline;
            l_Timeline.value = ++m_Value;
            l_Timeline.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            if (endsFrame)
            {
                m_Signals.push_back(l_Timeline);
                m_FrameValue = m_Value;
            }

            VkCommandBufferSubmitInfo l_Commands = MakeInfo<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
            l_Commands.commandBuffer = commandBuffer;

            VkSubmitInfo2 l_Submit = MakeInfo<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
            l_Submit.waitSemaphoreInfoCount = static_cast<std::uint32_t>(m_Waits.size());
            l_Submit.pWaitSemaphoreInfos = m_Waits.data();
            l_Submit.commandBufferInfoCount = 1;
            l_Submit.pCommandBufferInfos = &l_Commands;
            l_Submit.signalSemaphoreInfoCount = endsFrame ? static_cast<std::uint32_t>(m_Signals.size()) : 1;
            l_Submit.pSignalSemaphoreInfos = endsFrame ? m_Signals.data() : &l_Timeline;

            const VkResult l_Result = vkQueueSubmit2(m_Device.GetQueue(), 1, &l_Submit, VK_NULL_HANDLE);
            m_Waits.clear();

            return l_Result;
        }

        VkResult VulkanFrameSync::SubmitUploads(VkCommandBuffer commandBuffer, std::uint64_t graphicsValue)
        {
            return SubmitAfterGraphics(m_Device.GetTransferQueue(), commandBuffer, graphicsValue, m_UploadTimeline, m_UploadValue);
        }

        VkResult VulkanFrameSync::SubmitCompute(VkCommandBuffer commandBuffer, std::uint64_t graphicsValue)
        {
            return SubmitAfterGraphics(m_Device.GetComputeQueue(), commandBuffer, graphicsValue, m_ComputeTimeline, m_ComputeValue);
        }

        // Every stage of the frame's next submission waits, so what reads the work's output runs after
        VkResult VulkanFrameSync::SubmitAfterGraphics(VkQueue queue, VkCommandBuffer commandBuffer, std::uint64_t graphicsValue, VkSemaphore timeline, std::uint64_t& value)
        {
            VkSemaphoreSubmitInfo l_Wait = MakeInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            l_Wait.semaphore = m_Timeline;
            l_Wait.value = graphicsValue;
            l_Wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkSemaphoreSubmitInfo l_Signal = MakeInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            l_Signal.semaphore = timeline;
            l_Signal.value = ++value;
            l_Signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkCommandBufferSubmitInfo l_Commands = MakeInfo<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
            l_Commands.commandBuffer = commandBuffer;

            VkSubmitInfo2 l_Submit = MakeInfo<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
            l_Submit.waitSemaphoreInfoCount = graphicsValue != 0 ? 1 : 0;
            l_Submit.pWaitSemaphoreInfos = &l_Wait;
            l_Submit.commandBufferInfoCount = 1;
            l_Submit.pCommandBufferInfos = &l_Commands;
            l_Submit.signalSemaphoreInfoCount = 1;
            l_Submit.pSignalSemaphoreInfos = &l_Signal;

            const VkResult l_Result = vkQueueSubmit2(queue, 1, &l_Submit, VK_NULL_HANDLE);
            if (l_Result == VK_SUCCESS)
            {
                WaitForSemaphore(timeline);
                m_Waits.back().value = value;
            }

            return l_Result;
        }
    }
}