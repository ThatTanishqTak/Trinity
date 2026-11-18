#pragma once

#include "Trinity/RHI/Bindless.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstdint>
#include <string>

namespace Trinity
{
    namespace RHI
    {
        class VulkanDevice;

        // The bindings Slang gives descriptor handles when Bindless.slang turns off mutable descriptors
        constexpr std::uint32_t c_SamplerBinding = 0;
        constexpr std::uint32_t c_SampledImageBinding = 2;
        constexpr std::uint32_t c_StorageImageBinding = 3;
        constexpr std::uint32_t c_StorageBufferBinding = 7;

        // One update-after-bind set holds every descriptor, on the layout every pipeline shares
        class VulkanBindless
        {
        public:
            explicit VulkanBindless(const VulkanDevice& device);
            ~VulkanBindless();

            VulkanBindless(const VulkanBindless&) = delete;
            VulkanBindless& operator=(const VulkanBindless&) = delete;

            // False when the device allows too few bindless descriptors, or the set cannot be created
            [[nodiscard]] bool Initialize(std::string& error);
            // Destroys the pipeline layout, the pool with its set, then the set layout. Once only
            void Destroy();

            // c_NoBindlessIndex once every index is in use
            [[nodiscard]] std::uint32_t AddBufferDescriptor(VkBuffer buffer);
            [[nodiscard]] std::uint32_t AddImageDescriptor(std::uint32_t binding, VkDescriptorType type, VkImageView view, VkImageLayout layout);
            // c_NoBindlessIndex once every index is in use
            [[nodiscard]] std::uint32_t AddSamplerDescriptor(VkSampler sampler);
            void FreeResourceIndex(std::uint32_t index);
            void FreeSamplerIndex(std::uint32_t index);

            [[nodiscard]] VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }
            [[nodiscard]] VkDescriptorSet GetSet() const { return m_Set; }

        private:
            const VulkanDevice& m_Device;

            VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
            VkDescriptorPool m_Pool = VK_NULL_HANDLE;
            VkDescriptorSet m_Set = VK_NULL_HANDLE;
            VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
            IndexAllocator m_ResourceIndices;
            IndexAllocator m_SamplerIndices;
        };
    }
}