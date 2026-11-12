#include "Trinity/RHI/Vulkan/VulkanQueryPool.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <cmath>
#include <cstdint>
#include <optional>

namespace Trinity
{
    namespace RHI
    {
        // Reset on the host at once, so its first timestamps need no reset recorded before them
        std::optional<VulkanQueryPool> VulkanQueryPool::Create(const VulkanDevice& device, const QueryPoolDescription& description)
        {
            TR_CORE_ASSERT(description.Count != 0, "Query pool '{}' has no queries.", description.DebugName);
            if (!device.HasTimestamps())
            {
                TR_CORE_ERROR("Vulkan: query pool '{}' could not be created, since {} has no timestamps on its graphics queue", description.DebugName, device.GetInfo().AdapterName);

                return {};
            }

            VkQueryPoolCreateInfo l_Create = MakeInfo<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
            l_Create.queryType = VK_QUERY_TYPE_TIMESTAMP;
            l_Create.queryCount = description.Count;

            VulkanQueryPool l_Pool;
            l_Pool.Count = description.Count;
            const VkResult l_Result = vkCreateQueryPool(device.GetHandle(), &l_Create, nullptr, &l_Pool.Pool);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: query pool '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            vkResetQueryPool(device.GetHandle(), l_Pool.Pool, 0, description.Count);
            device.SetDebugName(VK_OBJECT_TYPE_QUERY_POOL, reinterpret_cast<std::uint64_t>(l_Pool.Pool), description.DebugName);

            return l_Pool;
        }

        // Vulkan gives nanoseconds per tick
        std::uint64_t VulkanQueryPool::GetTimestampFrequency(const VulkanDevice& device)
        {
            return static_cast<std::uint64_t>(std::llround(1000000000.0 / static_cast<double>(device.GetTimestampPeriod())));
        }

        VulkanRelease VulkanQueryPool::ToRelease() const
        {
            VulkanRelease l_Release;
            l_Release.QueryPool = Pool;

            return l_Release;
        }
    }
}