#include "Trinity/trpch.h"

#include "Trinity/Renderer/Material.h"
#include "Trinity/Renderer/Texture.h"
#include "Trinity/Vulkan/VulkanContext.h"

#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    Material::Material(VulkanContext* context) : m_Context(context)
    {
    }

    void Material::SetTexture(uint32_t binding, const Texture* texture)
    {
        m_Textures[binding] = texture;
    }

    void Material::SetSampler(uint32_t binding, VkSampler sampler)
    {
        m_Samplers[binding] = sampler;
    }

    void Material::SetConstants(const void* data, size_t size)
    {
        const uint8_t* l_Data = static_cast<const uint8_t*>(data);
        m_Constants.assign(l_Data, l_Data + size);
    }

    bool Material::Build(VkDescriptorSetLayout layout, VkDescriptorPool pool)
    {
        VkDescriptorSetAllocateInfo l_AllocInfo{};
        l_AllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        l_AllocInfo.descriptorPool = pool;
        l_AllocInfo.descriptorSetCount = 1;
        l_AllocInfo.pSetLayouts = &layout;

        if (vkAllocateDescriptorSets(m_Context->GetDevice(), &l_AllocInfo, &m_DescriptorSet) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to allocate material descriptor set");
            return false;
        }

        std::vector<VkWriteDescriptorSet> l_Writes;
        std::vector<VkDescriptorImageInfo> l_ImageInfos;

        for (auto& l_Pair : m_Textures)
        {
            VkDescriptorImageInfo l_Image{};
            l_Image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_Image.imageView = l_Pair.second->GetImageView();
            l_Image.sampler = VK_NULL_HANDLE;
            if (m_Samplers.count(l_Pair.first))
            {
                l_Image.sampler = m_Samplers[l_Pair.first];
            }
            l_ImageInfos.push_back(l_Image);

            VkWriteDescriptorSet l_Write{};
            l_Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_Write.dstSet = m_DescriptorSet;
            l_Write.dstBinding = l_Pair.first;
            l_Write.dstArrayElement = 0;
            l_Write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_Write.descriptorCount = 1;
            l_Write.pImageInfo = &l_ImageInfos.back();
            l_Writes.push_back(l_Write);
        }

        if (!l_Writes.empty())
        {
            vkUpdateDescriptorSets(m_Context->GetDevice(), static_cast<uint32_t>(l_Writes.size()), l_Writes.data(), 0, nullptr);
        }

        return true;
    }
}