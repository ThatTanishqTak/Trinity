#include "Trinity/RHI/Vulkan/VulkanBuffer.hpp"
#include "Trinity/RHI/Vulkan/VulkanAllocator.hpp"
#include "Trinity/RHI/Vulkan/VulkanBindless.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"

#include <optional>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            VkBufferUsageFlags ToVkBufferUsage(BufferUsage usage, MemoryType memory)
            {
                VkBufferUsageFlags l_Flags = 0;
                if (HasFlag(usage, BufferUsage::ShaderResource) || HasFlag(usage, BufferUsage::UnorderedAccess))
                {
                    l_Flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                }

                if (HasFlag(usage, BufferUsage::Index))
                {
                    l_Flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                }

                if (HasFlag(usage, BufferUsage::Indirect))
                {
                    l_Flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
                }

                if (HasFlag(usage, BufferUsage::CopySource) || memory == MemoryType::Upload)
                {
                    l_Flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                }

                if (HasFlag(usage, BufferUsage::CopyDestination) || memory == MemoryType::Readback)
                {
                    l_Flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                }

                return l_Flags;
            }
        }

        std::optional<VulkanBuffer> VulkanBuffer::Create(const VulkanDevice& device, const VulkanAllocator& allocator, VulkanBindless& bindless, const BufferDescription& description)
        {
            TR_CORE_ASSERT(description.Size != 0, "Buffer '{}' has no size.", description.DebugName);

            VkBufferCreateInfo l_Create = MakeInfo<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
            l_Create.size = description.Size;
            l_Create.usage = ToVkBufferUsage(description.Usage, description.Memory);
            l_Create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            TR_CORE_ASSERT(l_Create.usage != 0, "Buffer '{}' has no usage.", description.DebugName);

            // Upload and Readback memory is coherent, so writes and reads through the mapping need no flush or invalidate
            VmaAllocationCreateInfo l_Allocation{};
            switch (description.Memory)
            {
                case MemoryType::GPU:
                {
                    l_Allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

                    break;
                }
                case MemoryType::Upload:
                {
                    l_Allocation.usage = VMA_MEMORY_USAGE_AUTO;
                    l_Allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                    l_Allocation.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

                    break;
                }
                case MemoryType::Readback:
                {
                    l_Allocation.usage = VMA_MEMORY_USAGE_AUTO;
                    l_Allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                    l_Allocation.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                    l_Allocation.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

                    break;
                }
            }

            VulkanBuffer l_Buffer;
            VmaAllocationInfo l_Info{};
            const VkResult l_Result = vmaCreateBuffer(allocator.GetHandle(), &l_Create, &l_Allocation, &l_Buffer.Buffer, &l_Buffer.Allocation, &l_Info);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: buffer '{}' of {} could not be created ({})", description.DebugName, Memory::FormatBytes(description.Size), FormatResult(l_Result));

                return {};
            }

            l_Buffer.Mapped = static_cast<std::byte*>(l_Info.pMappedData);
            l_Buffer.Size = description.Size;
            device.SetDebugName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(l_Buffer.Buffer), description.DebugName);

            // Both views of a buffer are the same storage buffer descriptor, and Slang marks the read-only one NonWritable
            if (HasFlag(description.Usage, BufferUsage::ShaderResource))
            {
                l_Buffer.ShaderResourceIndex = bindless.AddBufferDescriptor(l_Buffer.Buffer);
            }

            if (HasFlag(description.Usage, BufferUsage::UnorderedAccess))
            {
                l_Buffer.UnorderedAccessIndex = bindless.AddBufferDescriptor(l_Buffer.Buffer);
            }

            return l_Buffer;
        }

        VulkanRelease VulkanBuffer::ToRelease() const
        {
            VulkanRelease l_Release;
            l_Release.Buffer = Buffer;
            l_Release.Allocation = Allocation;
            l_Release.ShaderResourceIndex = ShaderResourceIndex;
            l_Release.UnorderedAccessIndex = UnorderedAccessIndex;

            return l_Release;
        }
    }
}