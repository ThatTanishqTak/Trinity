#pragma once

#include "Trinity/RHI/Device.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        class VulkanInstance;

        // The physical device is chosen against the features the engine is built on
        class VulkanDevice
        {
        public:
            explicit VulkanDevice(const VulkanInstance& instance);
            ~VulkanDevice();

            VulkanDevice(const VulkanDevice&) = delete;
            VulkanDevice& operator=(const VulkanDevice&) = delete;

            [[nodiscard]] bool Initialize(const DeviceSpecification& specification, std::string& error);
            // Call once everything made on the device is gone. Does nothing a second time
            void Destroy();
            // Blocks until every queue has finished all its work
            void WaitIdle() const;

            void SetDebugName(VkObjectType type, std::uint64_t handle, std::string_view name) const;

            [[nodiscard]] const DeviceInfo& GetInfo() const { return m_Info; }
            [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
            [[nodiscard]] VkDevice GetHandle() const { return m_Device; }
            [[nodiscard]] VkQueue GetQueue() const { return m_Queue; }
            [[nodiscard]] std::uint32_t GetQueueFamily() const { return m_QueueFamily; }
            // The graphics queue when renderer.copy_queue is off or the device has no transfer family
            [[nodiscard]] VkQueue GetTransferQueue() const { return m_TransferQueue; }
            [[nodiscard]] std::uint32_t GetTransferFamily() const { return m_TransferFamily; }
            // The graphics queue when renderer.async_compute is off or the device has no compute family
            [[nodiscard]] VkQueue GetComputeQueue() const { return m_ComputeQueue; }
            [[nodiscard]] std::uint32_t GetComputeFamily() const { return m_ComputeFamily; }
            [[nodiscard]] bool HasComputeTimestamps() const { return m_ComputeTimestamps; }
            [[nodiscard]] bool HasTextureCompressionBC() const { return m_TextureCompressionBC; }
            [[nodiscard]] bool HasTimestamps() const { return m_TimestampsSupported; }
            [[nodiscard]] bool HasMemoryBudget() const { return m_MemoryBudget; }
            [[nodiscard]] float GetTimestampPeriod() const { return m_TimestampPeriod; }

        private:
            const VulkanInstance& m_Instance;

            DeviceInfo m_Info;
            bool m_TextureCompressionBC = false;
            bool m_TimestampsSupported = false;
            bool m_MemoryBudget = false;
            float m_TimestampPeriod = 1.0f;

            VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
            VkDevice m_Device = VK_NULL_HANDLE;
            VkQueue m_Queue = VK_NULL_HANDLE;
            std::uint32_t m_QueueFamily = 0;
            VkQueue m_TransferQueue = VK_NULL_HANDLE;
            std::uint32_t m_TransferFamily = 0;
            VkQueue m_ComputeQueue = VK_NULL_HANDLE;
            std::uint32_t m_ComputeFamily = 0;
            bool m_ComputeTimestamps = false;
        };
    }
}