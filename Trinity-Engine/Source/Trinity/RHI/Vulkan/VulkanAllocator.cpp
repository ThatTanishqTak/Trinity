#include "Trinity/RHI/Vulkan/VulkanAllocator.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanInstance.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Memory.hpp"

#include <cstddef>
#include <format>
#include <string>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            // VulkanMemoryAllocator's bookkeeping, and what the driver allocates for the objects it creates through it, counted under the Renderer tag
            void* VKAPI_PTR AllocateHostMemory([[maybe_unused]] void* userData, std::size_t size, std::size_t alignment, [[maybe_unused]] VkSystemAllocationScope scope)
            {
                return Memory::TryAllocate(size, MemoryTag::Renderer, alignment);
            }

            void* VKAPI_PTR ReallocateHostMemory([[maybe_unused]] void* userData, void* original, std::size_t size, std::size_t alignment, [[maybe_unused]] VkSystemAllocationScope scope)
            {
                return Memory::Reallocate(original, size, MemoryTag::Renderer, alignment);
            }

            void VKAPI_PTR FreeHostMemory([[maybe_unused]] void* userData, void* memory)
            {
                Memory::Free(memory);
            }

            constexpr VkAllocationCallbacks c_HostAllocator{ nullptr, &AllocateHostMemory, &ReallocateHostMemory, &FreeHostMemory, nullptr, nullptr };
        }

        VulkanAllocator::VulkanAllocator(const VulkanInstance& instance, const VulkanDevice& device) : m_Instance(instance), m_Device(device)
        {

        }

        VulkanAllocator::~VulkanAllocator()
        {
            Destroy();
        }

        bool VulkanAllocator::Initialize(std::string& error)
        {
            VmaAllocatorCreateInfo l_Create{};
            l_Create.physicalDevice = m_Device.GetPhysicalDevice();
            l_Create.device = m_Device.GetHandle();
            l_Create.instance = m_Instance.GetHandle();
            l_Create.vulkanApiVersion = VK_API_VERSION_1_3;
            l_Create.pAllocationCallbacks = &c_HostAllocator;

            VmaVulkanFunctions l_Functions{};
            VkResult l_Result = vmaImportVulkanFunctionsFromVolk(&l_Create, &l_Functions);
            if (l_Result == VK_SUCCESS)
            {
                l_Create.pVulkanFunctions = &l_Functions;
                l_Result = vmaCreateAllocator(&l_Create, &m_Allocator);
            }

            if (l_Result != VK_SUCCESS)
            {
                error = std::format("VulkanMemoryAllocator could not be created on {} ({})", m_Device.GetInfo().AdapterName, FormatResult(l_Result));

                return false;
            }

            return true;
        }

        void VulkanAllocator::Destroy()
        {
            if (m_Allocator != nullptr)
            {
                vmaDestroyAllocator(m_Allocator);
                m_Allocator = nullptr;
            }
        }
    }
}