#pragma once

#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstdint>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;

        // One timeline semaphore for the frame's submissions, and one each for uploads and compute
        class VulkanFrameSync
        {
        public:
            explicit VulkanFrameSync(const VulkanDevice& device);

            [[nodiscard]] VkResult Initialize();
            void Destroy();

            // A value of 0 needs no wait, since no frame signals it
            void WaitForTimeline(std::uint64_t value) const;
            // The frame waits on and signals nothing until the swap chains add their semaphores
            void BeginFrame();
            void WaitForSemaphore(VkSemaphore semaphore);
            void SignalSemaphore(VkSemaphore semaphore);
            // Submits the frame's commands so far. The one ending the frame signals the swap chains too
            [[nodiscard]] VkResult Submit(VkCommandBuffer commandBuffer, bool endsFrame);
            // Waits for the graphics timeline to reach the value when it is not 0. The frame waits for them
            [[nodiscard]] VkResult SubmitUploads(VkCommandBuffer commandBuffer, std::uint64_t graphicsValue);
            // As SubmitUploads, with the frame's later commands waiting for the compute work
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