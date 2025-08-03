#pragma once

#include <filesystem>
#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;

    class Texture
    {
    public:
        Texture() = default;
        Texture(VulkanContext* context);
        ~Texture() = default;

        bool LoadFromFile(const std::filesystem::path& path, int width, int height);
        void Destroy();

        VkImageView GetImageView() const { return m_ImageView; }
        VkSampler GetSampler() const { return m_Sampler; }

    private:
        VulkanContext* m_Context = nullptr;
        VkImage m_Image = VK_NULL_HANDLE;
        VkDeviceMemory m_ImageMemory = VK_NULL_HANDLE;
        VkImageView m_ImageView = VK_NULL_HANDLE;
        VkSampler m_Sampler = VK_NULL_HANDLE;
    };
}