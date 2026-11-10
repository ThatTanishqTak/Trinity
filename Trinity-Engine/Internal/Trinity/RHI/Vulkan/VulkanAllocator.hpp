#pragma once

#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <string>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;
        class VulkanInstance;

        // VulkanMemoryAllocator on the device, whose bookkeeping and what the driver allocates through it are counted under the Renderer tag
        class VulkanAllocator
        {
        public:
            VulkanAllocator(const VulkanInstance& instance, const VulkanDevice& device);
            ~VulkanAllocator();

            VulkanAllocator(const VulkanAllocator&) = delete;
            VulkanAllocator& operator=(const VulkanAllocator&) = delete;

            // False, with the reason in error, when VulkanMemoryAllocator cannot be created
            [[nodiscard]] bool Initialize(std::string& error);
            // Once every buffer and image made through it is gone. Does nothing a second time
            void Destroy();

            [[nodiscard]] VmaAllocator GetHandle() const { return m_Allocator; }

        private:
            const VulkanInstance& m_Instance;
            const VulkanDevice& m_Device;

            VmaAllocator m_Allocator = nullptr;
        };
    }
}