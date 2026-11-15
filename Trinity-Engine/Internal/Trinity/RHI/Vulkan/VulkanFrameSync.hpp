#pragma once

#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstdint>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;

        // The timeline semaphore each of a frame's submissions signals as its commands finish, the ones each frame's uploads and compute work signal, and the semaphores the swap chains have the frame's submissions wait on and signal
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
            // The frame's commands so far on the graphics queue, with the waits added since the last submission, the timeline reaching its next value once they have finished. The submission that ends the frame signals the swap chains' semaphores too
            [[nodiscard]] VkResult Submit(VkCommandBuffer commandBuffer, bool endsFrame);
            // The frame's uploads on the transfer queue, after the graphics timeline reaches the value when it is not 0, and the frame's commands waiting for them
            [[nodiscard]] VkResult SubmitUploads(VkCommandBuffer commandBuffer, std::uint64_t graphicsValue);
            // The frame's compute work on the compute queue, after the graphics timeline reaches the value when it is not 0, and the frame's later commands waiting for it
            [[nodiscard]] VkResult SubmitCompute(VkCommandBuffer commandBuffer, std::uint64_t graphicsValue);

            // The value the graphics timeline reaches once everything submitted to it so far has finished
            [[nodiscard]] std::uint64_t GetSubmittedValue() const { return m_Value; }
            // The value it reaches once the last frame that ended has finished
            [[nodiscard]] std::uint64_t GetFrameValue() const { return m_FrameValue; }

        private:
            [[nodiscard]] VkResult SubmitAfterGraphics(VkQueue queue, VkCommandBuffer commandBuffer, std::uint64_t graphicsValue, VkSemaphore timeline, std::uint64_t& value);

            const VulkanDevice& m_Device;

            VkSemaphore m_Timeline = VK_NULL_HANDLE;
            std::uint64_t m_Value = 0;
            std::uint64_t m_FrameValue = 0;
            VkSemaphore m_UploadTimeline = VK_NULL_HANDLE;
            std::uint64_t m_UploadValue = 0;
            VkSemaphore m_ComputeTimeline = VK_NULL_HANDLE;
            std::uint64_t m_ComputeValue = 0;
            std::vector<VkSemaphoreSubmitInfo> m_Waits;
            std::vector<VkSemaphoreSubmitInfo> m_Signals;
        };
    }
}