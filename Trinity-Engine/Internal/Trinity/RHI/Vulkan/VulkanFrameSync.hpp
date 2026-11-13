#pragma once

#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstdint>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;

        // The timeline semaphore each frame signals as its commands finish, and the semaphores the swap chains have the frame's submission wait on and signal
        class VulkanFrameSync
        {
        public:
            explicit VulkanFrameSync(const VulkanDevice& device);

            // VK_SUCCESS, or the result creating the timeline semaphore
            [[nodiscard]] VkResult Initialize();
            void Destroy();

            // Blocks until the timeline reaches the value, which needs no wait when it is 0, since no frame signals that
            void WaitForTimeline(std::uint64_t value) const;
            // The frame waits on and signals nothing until the swap chains add their semaphores
            void BeginFrame();
            void WaitForSemaphore(VkSemaphore semaphore);
            void SignalSemaphore(VkSemaphore semaphore);
            // The frame's commands on the graphics queue with its waits and signals, the timeline reaching the value once they have finished
            [[nodiscard]] VkResult Submit(VkCommandBuffer commandBuffer, std::uint64_t value);

        private:
            const VulkanDevice& m_Device;

            VkSemaphore m_Timeline = VK_NULL_HANDLE;
            std::vector<VkSemaphoreSubmitInfo> m_Waits;
            std::vector<VkSemaphoreSubmitInfo> m_Signals;
        };
    }
}