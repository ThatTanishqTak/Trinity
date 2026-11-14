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

        // The physical device chosen against the features the engine is built on, the logical device made on it with its graphics and compute queue, and the debug names its objects are given
        class VulkanDevice
        {
        public:
            explicit VulkanDevice(const VulkanInstance& instance);
            ~VulkanDevice();

            VulkanDevice(const VulkanDevice&) = delete;
            VulkanDevice& operator=(const VulkanDevice&) = delete;

            // False, with the reason in error, when no physical device can run the engine or the logical device cannot be created
            [[nodiscard]] bool Initialize(const DeviceSpecification& specification, std::string& error);
            // The logical device, once everything made on it is gone. Does nothing a second time
            void Destroy();
            // Blocks until every queue has finished all its work
            void WaitIdle() const;

            void SetDebugName(VkObjectType type, std::uint64_t handle, std::string_view name) const;

            [[nodiscard]] const DeviceInfo& GetInfo() const { return m_Info; }
            [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
            [[nodiscard]] VkDevice GetHandle() const { return m_Device; }
            [[nodiscard]] VkQueue GetQueue() const { return m_Queue; }
            [[nodiscard]] std::uint32_t GetQueueFamily() const { return m_QueueFamily; }
            // The dedicated transfer family's queue when renderer.copy_queue is on and the device has one, and otherwise the graphics queue, where uploads go in a submission of their own
            [[nodiscard]] VkQueue GetTransferQueue() const { return m_TransferQueue; }
            [[nodiscard]] std::uint32_t GetTransferFamily() const { return m_TransferFamily; }
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
        };
    }
}