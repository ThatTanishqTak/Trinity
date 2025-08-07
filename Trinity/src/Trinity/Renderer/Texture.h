#pragma once

#include <optional>
#include <filesystem>
#include <vector>

#include <vulkan/vulkan.h>

namespace Trinity
{
    class VulkanContext;

    class Texture
    {
    public:
        Texture() = default;
        Texture(VulkanContext* context);
        ~Texture();

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&& other) noexcept;
        Texture& operator=(Texture&& other) noexcept;

        std::optional<std::string> LoadFromFile(const std::filesystem::path& path, int width, int height);
        std::optional<std::string> CreateFromPixels(const std::vector<std::byte>& pixels, int width, int height);
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