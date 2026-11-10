#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Bindless.hpp"
#include "Trinity/RHI/ReleaseQueue.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstdint>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        class VulkanAllocator;
        class VulkanDevice;

        using VulkanImageViews = std::vector<VkImageView, TaggedAllocator<VkImageView, MemoryTag::Renderer>>;

        // What a destroyed buffer, texture, sampler, pipeline or query pool leaves to destroy, and the bindless indices it frees
        struct VulkanRelease
        {
            VkBuffer Buffer = VK_NULL_HANDLE;
            VkImage Image = VK_NULL_HANDLE;
            VulkanImageViews Views;
            VmaAllocation Allocation = nullptr;
            VkImageView SampledView = VK_NULL_HANDLE;
            VkPipeline Pipeline = VK_NULL_HANDLE;
            VkSampler Sampler = VK_NULL_HANDLE;
            VkQueryPool QueryPool = VK_NULL_HANDLE;
            std::uint32_t ShaderResourceIndex = c_NoBindlessIndex;
            std::uint32_t UnorderedAccessIndex = c_NoBindlessIndex;
            std::uint32_t SamplerIndex = c_NoBindlessIndex;
        };

        // Destroyed objects kept until no frame in flight can still use them, then destroyed with their memory and their bindless indices freed
        class VulkanReleaseQueue
        {
        public:
            VulkanReleaseQueue(const VulkanDevice& device, const VulkanAllocator& allocator, IndexAllocator& resourceIndices, IndexAllocator& samplerIndices);

            void Push(VulkanRelease release);
            // Once the frame c_FramesInFlight before this one has finished on the GPU
            void BeginFrame();
            void EndFrame();
            // Once the GPU is idle, keeping what the frame being recorded destroyed
            void ReleaseIdle();
            void ReleaseAll();
            // At once, for what is still alive when the device goes
            void Release(const VulkanRelease& release);

        private:
            const VulkanDevice& m_Device;
            const VulkanAllocator& m_Allocator;
            IndexAllocator& m_ResourceIndices;
            IndexAllocator& m_SamplerIndices;

            ReleaseQueue<VulkanRelease> m_Releases;
        };
    }
}