#include "Trinity/RHI/Vulkan/VulkanCommandPool.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include <cstdint>

namespace Trinity
{
    namespace RHI
    {
        VkResult VulkanCommandPool::Create(const VulkanDevice& device, std::uint32_t queueFamily)
        {
            VkCommandPoolCreateInfo l_Pool = MakeInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
            l_Pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            l_Pool.queueFamilyIndex = queueFamily;
            const VkResult l_Result = vkCreateCommandPool(device.GetHandle(), &l_Pool, nullptr, &Pool);
            if (l_Result != VK_SUCCESS)
            {
                return l_Result;
            }

            VkCommandBufferAllocateInfo l_Allocate = MakeInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
            l_Allocate.commandPool = Pool;
            l_Allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            l_Allocate.commandBufferCount = 1;

            return vkAllocateCommandBuffers(device.GetHandle(), &l_Allocate, &CommandBuffer);
        }

        void VulkanCommandPool::Reset(const VulkanDevice& device) const
        {
            vkResetCommandPool(device.GetHandle(), Pool, 0);
        }

        void VulkanCommandPool::Destroy(const VulkanDevice& device) const
        {
            vkDestroyCommandPool(device.GetHandle(), Pool, nullptr);
        }
    }
}