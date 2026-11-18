#pragma once

#include "Trinity/RHI/Device.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;
        class VulkanInstance;

        // Its bookkeeping and the driver's allocations through it are counted under the Renderer tag
        class VulkanAllocator
        {
        public:
            VulkanAllocator(const VulkanInstance& instance, const VulkanDevice& device);
            ~VulkanAllocator();

            VulkanAllocator(const VulkanAllocator&) = delete;
            VulkanAllocator& operator=(const VulkanAllocator&) = delete;

            [[nodiscard]] bool Initialize(std::string& error);
            // Call once every buffer and image made through it is gone. Does nothing a second time
            void Destroy();
            // VulkanMemoryAllocator asks to be told once a frame
            void BeginFrame(std::uint64_t frame);

            // With the driver's budget and use when the device has VK_EXT_memory_budget
            [[nodiscard]] std::vector<MemoryHeapBudget> GetMemoryBudget() const;

            [[nodiscard]] VmaAllocator GetHandle() const { return m_Allocator; }

        private:
            const VulkanInstance& m_Instance;
            const VulkanDevice& m_Device;

            VmaAllocator m_Allocator = nullptr;
        };
    }
}