#pragma once

#include "Trinity/RHI/Bindless.hpp"
#include "Trinity/RHI/Resources.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"
#include "Trinity/RHI/Vulkan/VulkanReleaseQueue.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Trinity
{
    namespace RHI
    {
        class VulkanAllocator;
        class VulkanBindless;
        class VulkanDevice;

        // A buffer in VulkanMemoryAllocator's memory, mapped for good when the CPU writes or reads it, with a bindless index for each way shaders reach it
        struct VulkanBuffer
        {
            // Empty, with the reason logged, when the buffer cannot be created
            [[nodiscard]] static std::optional<VulkanBuffer> Create(const VulkanDevice& device, const VulkanAllocator& allocator, VulkanBindless& bindless, const BufferDescription& description);
            // What destroying it leaves for the release queue
            [[nodiscard]] VulkanRelease ToRelease() const;

            VkBuffer Buffer = VK_NULL_HANDLE;
            VmaAllocation Allocation = nullptr;
            std::byte* Mapped = nullptr;
            std::uint64_t Size = 0;
            std::uint32_t ShaderResourceIndex = c_NoBindlessIndex;
            std::uint32_t UnorderedAccessIndex = c_NoBindlessIndex;
        };
    }
}