#include "Trinity/RHI/Vulkan/VulkanBindless.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            struct BindlessLimit
            {
                std::string_view Name;
                std::uint32_t Allowed = 0;
                std::uint32_t Needed = 0;
            };
        }

        VulkanBindless::VulkanBindless(const VulkanDevice& device) : m_Device(device)
        {

        }

        VulkanBindless::~VulkanBindless()
        {
            Destroy();
        }

        // One set holds every descriptor, and every pipeline shares its layout and the push constant range, so the set is bound once per frame
        bool VulkanBindless::Initialize(std::string& error)
        {
            VkPhysicalDeviceVulkan12Properties l_Limits = MakeInfo<VkPhysicalDeviceVulkan12Properties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES);
            VkPhysicalDeviceProperties2 l_Properties = MakeInfo<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
            l_Properties.pNext = &l_Limits;
            vkGetPhysicalDeviceProperties2(m_Device.GetPhysicalDevice(), &l_Properties);

            const std::array<BindlessLimit, 5> l_Checks
            { {
                { "samplers", std::min(l_Limits.maxPerStageDescriptorUpdateAfterBindSamplers, l_Limits.maxDescriptorSetUpdateAfterBindSamplers), c_BindlessSamplerCapacity },
                { "sampled images", std::min(l_Limits.maxPerStageDescriptorUpdateAfterBindSampledImages, l_Limits.maxDescriptorSetUpdateAfterBindSampledImages), c_BindlessResourceCapacity },
                { "storage images", std::min(l_Limits.maxPerStageDescriptorUpdateAfterBindStorageImages, l_Limits.maxDescriptorSetUpdateAfterBindStorageImages), c_BindlessResourceCapacity },
                { "storage buffers", std::min(l_Limits.maxPerStageDescriptorUpdateAfterBindStorageBuffers, l_Limits.maxDescriptorSetUpdateAfterBindStorageBuffers), c_BindlessResourceCapacity },
                { "descriptors per stage", l_Limits.maxPerStageUpdateAfterBindResources, c_BindlessSamplerCapacity + 3 * c_BindlessResourceCapacity }
            } };

            for (const BindlessLimit& it_Check : l_Checks)
            {
                if (it_Check.Allowed < it_Check.Needed)
                {
                    error = std::format("{} allows {} bindless {}, and the engine needs {}", m_Device.GetInfo().AdapterName, it_Check.Allowed, it_Check.Name, it_Check.Needed);

                    return false;
                }
            }

            const std::array<VkDescriptorSetLayoutBinding, 4> l_Bindings
            { {
                { c_SamplerBinding, VK_DESCRIPTOR_TYPE_SAMPLER, c_BindlessSamplerCapacity, VK_SHADER_STAGE_ALL, nullptr },
                { c_SampledImageBinding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, c_BindlessResourceCapacity, VK_SHADER_STAGE_ALL, nullptr },
                { c_StorageImageBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, c_BindlessResourceCapacity, VK_SHADER_STAGE_ALL, nullptr },
                { c_StorageBufferBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, c_BindlessResourceCapacity, VK_SHADER_STAGE_ALL, nullptr }
            } };

            constexpr VkDescriptorBindingFlags c_BindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
            std::array<VkDescriptorBindingFlags, 4> l_Flags{};
            l_Flags.fill(c_BindingFlags);

            VkDescriptorSetLayoutBindingFlagsCreateInfo l_BindingFlags = MakeInfo<VkDescriptorSetLayoutBindingFlagsCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
            l_BindingFlags.bindingCount = static_cast<std::uint32_t>(l_Flags.size());
            l_BindingFlags.pBindingFlags = l_Flags.data();

            VkDescriptorSetLayoutCreateInfo l_Layout = MakeInfo<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
            l_Layout.pNext = &l_BindingFlags;
            l_Layout.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            l_Layout.bindingCount = static_cast<std::uint32_t>(l_Bindings.size());
            l_Layout.pBindings = l_Bindings.data();

            VkResult l_Result = vkCreateDescriptorSetLayout(m_Device.GetHandle(), &l_Layout, nullptr, &m_Layout);
            if (l_Result == VK_SUCCESS)
            {
                const std::array<VkDescriptorPoolSize, 4> l_Sizes
                { {
                    { VK_DESCRIPTOR_TYPE_SAMPLER, c_BindlessSamplerCapacity },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, c_BindlessResourceCapacity },
                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, c_BindlessResourceCapacity },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, c_BindlessResourceCapacity }
                } };

                VkDescriptorPoolCreateInfo l_Pool = MakeInfo<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
                l_Pool.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
                l_Pool.maxSets = 1;
                l_Pool.poolSizeCount = static_cast<std::uint32_t>(l_Sizes.size());
                l_Pool.pPoolSizes = l_Sizes.data();
                l_Result = vkCreateDescriptorPool(m_Device.GetHandle(), &l_Pool, nullptr, &m_Pool);
            }

            if (l_Result == VK_SUCCESS)
            {
                VkDescriptorSetAllocateInfo l_Allocate = MakeInfo<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
                l_Allocate.descriptorPool = m_Pool;
                l_Allocate.descriptorSetCount = 1;
                l_Allocate.pSetLayouts = &m_Layout;
                l_Result = vkAllocateDescriptorSets(m_Device.GetHandle(), &l_Allocate, &m_Set);
            }

            if (l_Result == VK_SUCCESS)
            {
                const VkPushConstantRange l_PushConstants{ VK_SHADER_STAGE_ALL, 0, c_MaxPushConstantSize };

                VkPipelineLayoutCreateInfo l_PipelineLayout = MakeInfo<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
                l_PipelineLayout.setLayoutCount = 1;
                l_PipelineLayout.pSetLayouts = &m_Layout;
                l_PipelineLayout.pushConstantRangeCount = 1;
                l_PipelineLayout.pPushConstantRanges = &l_PushConstants;
                l_Result = vkCreatePipelineLayout(m_Device.GetHandle(), &l_PipelineLayout, nullptr, &m_PipelineLayout);
            }

            if (l_Result != VK_SUCCESS)
            {
                error = std::format("the bindless descriptor set could not be created on {} ({})", m_Device.GetInfo().AdapterName, FormatResult(l_Result));

                return false;
            }

            m_Device.SetDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<std::uint64_t>(m_Set), "Bindless");
            m_ResourceIndices.Reset(c_BindlessResourceCapacity);
            m_SamplerIndices.Reset(c_BindlessSamplerCapacity);

            return true;
        }

        void VulkanBindless::Destroy()
        {
            if (m_PipelineLayout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(m_Device.GetHandle(), m_PipelineLayout, nullptr);
                m_PipelineLayout = VK_NULL_HANDLE;
            }

            if (m_Pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(m_Device.GetHandle(), m_Pool, nullptr);
                m_Pool = VK_NULL_HANDLE;
                m_Set = VK_NULL_HANDLE;
            }

            if (m_Layout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(m_Device.GetHandle(), m_Layout, nullptr);
                m_Layout = VK_NULL_HANDLE;
            }
        }

        std::uint32_t VulkanBindless::AddBufferDescriptor(VkBuffer buffer)
        {
            const std::uint32_t l_Index = m_ResourceIndices.Allocate();
            if (l_Index == c_NoBindlessIndex)
            {
                TR_CORE_ERROR("Vulkan: all {} bindless resource indices are in use", c_BindlessResourceCapacity);

                return c_NoBindlessIndex;
            }

            const VkDescriptorBufferInfo l_Info{ buffer, 0, VK_WHOLE_SIZE };

            VkWriteDescriptorSet l_Write = MakeInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            l_Write.dstSet = m_Set;
            l_Write.dstBinding = c_StorageBufferBinding;
            l_Write.dstArrayElement = l_Index;
            l_Write.descriptorCount = 1;
            l_Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            l_Write.pBufferInfo = &l_Info;
            vkUpdateDescriptorSets(m_Device.GetHandle(), 1, &l_Write, 0, nullptr);

            return l_Index;
        }

        std::uint32_t VulkanBindless::AddImageDescriptor(std::uint32_t binding, VkDescriptorType type, VkImageView view, VkImageLayout layout)
        {
            if (view == VK_NULL_HANDLE)
            {
                return c_NoBindlessIndex;
            }

            const std::uint32_t l_Index = m_ResourceIndices.Allocate();
            if (l_Index == c_NoBindlessIndex)
            {
                TR_CORE_ERROR("Vulkan: all {} bindless resource indices are in use", c_BindlessResourceCapacity);

                return c_NoBindlessIndex;
            }

            const VkDescriptorImageInfo l_Info{ VK_NULL_HANDLE, view, layout };

            VkWriteDescriptorSet l_Write = MakeInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            l_Write.dstSet = m_Set;
            l_Write.dstBinding = binding;
            l_Write.dstArrayElement = l_Index;
            l_Write.descriptorCount = 1;
            l_Write.descriptorType = type;
            l_Write.pImageInfo = &l_Info;
            vkUpdateDescriptorSets(m_Device.GetHandle(), 1, &l_Write, 0, nullptr);

            return l_Index;
        }

        std::uint32_t VulkanBindless::AddSamplerDescriptor(VkSampler sampler)
        {
            const std::uint32_t l_Index = m_SamplerIndices.Allocate();
            if (l_Index == c_NoBindlessIndex)
            {
                return c_NoBindlessIndex;
            }

            const VkDescriptorImageInfo l_Info{ sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };

            VkWriteDescriptorSet l_Write = MakeInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            l_Write.dstSet = m_Set;
            l_Write.dstBinding = c_SamplerBinding;
            l_Write.dstArrayElement = l_Index;
            l_Write.descriptorCount = 1;
            l_Write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            l_Write.pImageInfo = &l_Info;
            vkUpdateDescriptorSets(m_Device.GetHandle(), 1, &l_Write, 0, nullptr);

            return l_Index;
        }

        void VulkanBindless::FreeResourceIndex(std::uint32_t index)
        {
            m_ResourceIndices.Free(index);
        }

        void VulkanBindless::FreeSamplerIndex(std::uint32_t index)
        {
            m_SamplerIndices.Free(index);
        }
    }
}