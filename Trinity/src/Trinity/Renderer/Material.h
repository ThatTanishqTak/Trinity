#pragma once

#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;
    class Texture;

    class Material
    {
    public:
        Material() = default;
        explicit Material(VulkanContext* context);
        ~Material() = default;

        void SetTexture(uint32_t binding, const Texture* texture);
        void SetSampler(uint32_t binding, VkSampler sampler);
        void SetConstants(const void* data, size_t size);

        bool Build(VkDescriptorSetLayout layout, VkDescriptorPool pool);
        VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }

    private:
        VulkanContext* m_Context = nullptr;
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;

        std::unordered_map<uint32_t, const Texture*> m_Textures;
        std::unordered_map<uint32_t, VkSampler> m_Samplers;
        std::vector<uint8_t> m_Constants;
    };
}