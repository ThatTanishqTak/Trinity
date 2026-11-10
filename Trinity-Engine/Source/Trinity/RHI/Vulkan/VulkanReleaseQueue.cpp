#include "Trinity/RHI/Vulkan/VulkanReleaseQueue.hpp"
#include "Trinity/RHI/Vulkan/VulkanAllocator.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"

#include <utility>

namespace Trinity
{
    namespace RHI
    {
        VulkanReleaseQueue::VulkanReleaseQueue(const VulkanDevice& device, const VulkanAllocator& allocator, IndexAllocator& resourceIndices, IndexAllocator& samplerIndices) : m_Device(device), m_Allocator(allocator), m_ResourceIndices(resourceIndices), m_SamplerIndices(samplerIndices)
        {

        }

        void VulkanReleaseQueue::Push(VulkanRelease release)
        {
            m_Releases.Push(std::move(release));
        }

        void VulkanReleaseQueue::BeginFrame()
        {
            m_Releases.BeginFrame([this](const VulkanRelease& release) { Release(release); });
        }

        void VulkanReleaseQueue::EndFrame()
        {
            m_Releases.EndFrame();
        }

        void VulkanReleaseQueue::ReleaseIdle()
        {
            m_Releases.ReleaseIdle([this](const VulkanRelease& release) { Release(release); });
        }

        void VulkanReleaseQueue::ReleaseAll()
        {
            m_Releases.ReleaseAll([this](const VulkanRelease& release) { Release(release); });
        }

        // An index is only handed out again once the frames that could read it have finished
        void VulkanReleaseQueue::Release(const VulkanRelease& release)
        {
            m_ResourceIndices.Free(release.ShaderResourceIndex);
            m_ResourceIndices.Free(release.UnorderedAccessIndex);
            m_SamplerIndices.Free(release.SamplerIndex);

            if (release.Sampler != VK_NULL_HANDLE)
            {
                vkDestroySampler(m_Device.GetHandle(), release.Sampler, nullptr);
            }

            if (release.Pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_Device.GetHandle(), release.Pipeline, nullptr);
            }

            if (release.QueryPool != VK_NULL_HANDLE)
            {
                vkDestroyQueryPool(m_Device.GetHandle(), release.QueryPool, nullptr);
            }

            if (release.SampledView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(m_Device.GetHandle(), release.SampledView, nullptr);
            }

            for (VkImageView it_View : release.Views)
            {
                if (it_View != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(m_Device.GetHandle(), it_View, nullptr);
                }
            }

            if (release.Buffer != VK_NULL_HANDLE)
            {
                vmaDestroyBuffer(m_Allocator.GetHandle(), release.Buffer, release.Allocation);
            }

            if (release.Image != VK_NULL_HANDLE && release.Allocation != nullptr)
            {
                vmaDestroyImage(m_Allocator.GetHandle(), release.Image, release.Allocation);
            }
        }
    }
}