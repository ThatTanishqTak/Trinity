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

            return vkCreateSemaphore(m_Device.GetHandle(), &l_Semaphore, nullptr, &m_Timeline);
        }

        void VulkanFrameSync::Destroy()
        {
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

        VkResult VulkanFrameSync::Submit(VkCommandBuffer commandBuffer, std::uint64_t value)
        {
            SignalSemaphore(m_Timeline);
            m_Signals.back().value = value;

            VkCommandBufferSubmitInfo l_Commands = MakeInfo<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
            l_Commands.commandBuffer = commandBuffer;

            VkSubmitInfo2 l_Submit = MakeInfo<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
            l_Submit.waitSemaphoreInfoCount = static_cast<std::uint32_t>(m_Waits.size());
            l_Submit.pWaitSemaphoreInfos = m_Waits.data();
            l_Submit.commandBufferInfoCount = 1;
            l_Submit.pCommandBufferInfos = &l_Commands;
            l_Submit.signalSemaphoreInfoCount = static_cast<std::uint32_t>(m_Signals.size());
            l_Submit.pSignalSemaphoreInfos = m_Signals.data();

            return vkQueueSubmit2(m_Device.GetQueue(), 1, &l_Submit, VK_NULL_HANDLE);
        }
    }
}