#pragma once

#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstdint>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;

        // The timeline semaphore each frame signals as its commands finish, the one each frame's uploads signal, and the semaphores the swap chains have the frame's submission wait on and signal
        class VulkanFrameSync
        {
        public:
            explicit VulkanFrameSync(const VulkanDevice& device);

            // VK_SUCCESS, or the result creating the timeline semaphores
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
            // The frame's uploads on the transfer queue, after the graphics timeline reaches the value when it is not 0, and the frame's commands waiting for them
            [[nodiscard]] VkResult SubmitUploads(VkCommandBuffer commandBuffer, std::uint64_t graphicsValue);

        private:
            const VulkanDevice& m_Device;

            VkSemaphore m_Timeline = VK_NULL_HANDLE;
            VkSemaphore m_UploadTimeline = VK_NULL_HANDLE;
            std::uint64_t m_UploadValue = 0;
            std::vector<VkSemaphoreSubmitInfo> m_Waits;
            std::vector<VkSemaphoreSubmitInfo> m_Signals;
        };
    }
}