#pragma once

#include <vector>
#include <optional>
#include <string>
#include <cstddef>
#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;

    class Texture
    {
    public:
        explicit Texture(VulkanContext* context);

        std::optional<std::string> CreateFromPixels(const std::vector<std::byte>& pixels, int width, int height);
        void Destroy();

        VkImage GetImage() const { return m_Image; }
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