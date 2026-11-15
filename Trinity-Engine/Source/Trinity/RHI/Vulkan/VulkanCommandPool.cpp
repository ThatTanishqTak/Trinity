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

            // The first command buffer now, which every frame records into, and the rest when a frame first splits into more submissions
            return Allocate(device);
        }

        VkResult VulkanCommandPool::Allocate(const VulkanDevice& device)
        {
            VkCommandBufferAllocateInfo l_Allocate = MakeInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
            l_Allocate.commandPool = Pool;
            l_Allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            l_Allocate.commandBufferCount = 1;

            VkCommandBuffer l_CommandBuffer = VK_NULL_HANDLE;
            const VkResult l_Result = vkAllocateCommandBuffers(device.GetHandle(), &l_Allocate, &l_CommandBuffer);
            if (l_Result == VK_SUCCESS)
            {
                CommandBuffers.push_back(l_CommandBuffer);
            }

            return l_Result;
        }

        VkCommandBuffer VulkanCommandPool::Next(const VulkanDevice& device)
        {
            if (Used == CommandBuffers.size() && Allocate(device) != VK_SUCCESS)
            {
                return VK_NULL_HANDLE;
            }

            return CommandBuffers[Used++];
        }

        void VulkanCommandPool::Reset(const VulkanDevice& device)
        {
            vkResetCommandPool(device.GetHandle(), Pool, 0);
            Used = 0;
        }

        void VulkanCommandPool::Destroy(const VulkanDevice& device) const
        {
            vkDestroyCommandPool(device.GetHandle(), Pool, nullptr);
        }
    }
}