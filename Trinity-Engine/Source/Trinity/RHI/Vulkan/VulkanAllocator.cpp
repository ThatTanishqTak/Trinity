#include "Trinity/RHI/Vulkan/VulkanAllocator.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanInstance.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

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
            // D3D12MemoryAllocator's block size, so an allocation over 32 MiB gets memory of its own on both APIs and goes back to the driver when it is released
            l_Create.preferredLargeHeapBlockSize = 64ull * 1024 * 1024;
            l_Create.flags = m_Device.HasMemoryBudget() ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT : 0;

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

        void VulkanAllocator::BeginFrame(std::uint64_t frame)
        {
            vmaSetCurrentFrameIndex(m_Allocator, static_cast<std::uint32_t>(frame));
        }

        // Without the extension VulkanMemoryAllocator only guesses the budget, so the heap's size is all it reports
        std::vector<MemoryHeapBudget> VulkanAllocator::GetMemoryBudget() const
        {
            const VkPhysicalDeviceMemoryProperties* l_Properties = nullptr;
            vmaGetMemoryProperties(m_Allocator, &l_Properties);

            std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> l_Budgets{};
            vmaGetHeapBudgets(m_Allocator, l_Budgets.data());

            std::vector<MemoryHeapBudget> l_Heaps(l_Properties->memoryHeapCount);
            for (std::uint32_t it_Heap = 0; it_Heap < l_Properties->memoryHeapCount; ++it_Heap)
            {
                MemoryHeapBudget& l_Heap = l_Heaps[it_Heap];
                l_Heap.Size = l_Properties->memoryHeaps[it_Heap].size;
                l_Heap.DeviceLocal = (l_Properties->memoryHeaps[it_Heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
                l_Heap.FromDriver = m_Device.HasMemoryBudget();
                if (l_Heap.FromDriver)
                {
                    l_Heap.Budget = l_Budgets[it_Heap].budget;
                    l_Heap.Usage = l_Budgets[it_Heap].usage;
                }
            }

            return l_Heaps;
        }
    }
}