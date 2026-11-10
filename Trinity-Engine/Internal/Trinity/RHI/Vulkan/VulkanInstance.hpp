#pragma once

#include "Trinity/RHI/Device.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace Trinity
{
    namespace RHI
    {
        // The Vulkan loader and instance, with the layers and extensions asked for, and with validation the layer's messenger, whose warnings and errors reach the log and are counted
        class VulkanInstance
        {
        public:
            VulkanInstance() = default;
            ~VulkanInstance();

            VulkanInstance(const VulkanInstance&) = delete;
            VulkanInstance& operator=(const VulkanInstance&) = delete;

            // False, with the reason in error, when there is no Vulkan 1.3 loader or the instance cannot be created
            [[nodiscard]] bool Initialize(const DeviceSpecification& specification, std::string& error);
            // The messenger, then the instance, once the device is gone. Does nothing a second time
            void Destroy();

            [[nodiscard]] VkInstance GetHandle() const { return m_Instance; }
            [[nodiscard]] bool IsValidationEnabled() const { return m_Validation; }
            [[nodiscard]] std::uint32_t GetMessageCount() const { return m_MessageCount.load(); }

        private:
            static VKAPI_ATTR VkBool32 VKAPI_CALL OnDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void* context);

            [[nodiscard]] bool Create(const DeviceSpecification& specification, std::uint32_t loaderVersion, std::string& error);

            bool m_Validation = false;
            std::atomic<std::uint32_t> m_MessageCount{ 0 };

            VkInstance m_Instance = VK_NULL_HANDLE;
            VkDebugUtilsMessengerEXT m_Messenger = VK_NULL_HANDLE;
        };
    }
}