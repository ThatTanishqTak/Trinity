#pragma once

#include "Trinity/RHI/Bindless.hpp"
#include "Trinity/RHI/Resources.hpp"
#include "Trinity/RHI/Types.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"
#include "Trinity/RHI/Vulkan/VulkanReleaseQueue.hpp"

#include <cstdint>
#include <optional>

namespace Trinity
{
    namespace RHI
    {
        class VulkanAllocator;
        class VulkanBindless;
        class VulkanDevice;

        // Swap chain images have no allocation, since the swap chain owns them. A render target or depth texture has an attachment view per mip of each layer, numbered by GetSubresourceIndex
        struct VulkanTexture
        {
            // Empty, with the reason logged, when the image cannot be created. The context checks the description first
            [[nodiscard]] static std::optional<VulkanTexture> Create(const VulkanDevice& device, const VulkanAllocator& allocator, VulkanBindless& bindless, const TextureDescription& description);
            // A swap chain image taken in as a texture with a single attachment view
            [[nodiscard]] static VulkanTexture FromSwapChainImage(const VulkanDevice& device, VkImage image, Format format, std::uint32_t width, std::uint32_t height);
            [[nodiscard]] static bool IsFormatSupported(const VulkanDevice& device, Format format, TextureUsage usage, std::uint32_t sampleCount);

            // What destroying it leaves for the release queue
            [[nodiscard]] VulkanRelease ToRelease() const;
            // A swap chain image's view, destroyed at once
            void DestroySwapChainView(const VulkanDevice& device) const;

            VkImage Image = VK_NULL_HANDLE;
            VmaAllocation Allocation = nullptr;
            VulkanImageViews AttachmentViews;
            VkImageView SampledView = VK_NULL_HANDLE;
            VkImageView StorageView = VK_NULL_HANDLE;
            Format TextureFormat = Format::Unknown;
            TextureDimension Dimension = TextureDimension::Texture2D;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
            std::uint32_t MipLevels = 0;
            std::uint32_t ArrayLayers = 0;
            std::uint32_t SampleCount = 1;
            std::uint32_t ShaderResourceIndex = c_NoBindlessIndex;
            std::uint32_t UnorderedAccessIndex = c_NoBindlessIndex;
        };
    }
}