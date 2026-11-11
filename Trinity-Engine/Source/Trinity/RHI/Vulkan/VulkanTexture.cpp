#include "Trinity/RHI/Vulkan/VulkanTexture.hpp"
#include "Trinity/RHI/Vulkan/VulkanAllocator.hpp"
#include "Trinity/RHI/Vulkan/VulkanBindless.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            VkImageUsageFlags ToVkImageUsage(TextureUsage usage)
            {
                VkImageUsageFlags l_Flags = 0;
                if (HasFlag(usage, TextureUsage::ShaderResource))
                {
                    l_Flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
                }

                if (HasFlag(usage, TextureUsage::UnorderedAccess))
                {
                    l_Flags |= VK_IMAGE_USAGE_STORAGE_BIT;
                }

                if (HasFlag(usage, TextureUsage::RenderTarget))
                {
                    l_Flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                }

                if (HasFlag(usage, TextureUsage::DepthStencil))
                {
                    l_Flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                }

                if (HasFlag(usage, TextureUsage::CopySource))
                {
                    l_Flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                }

                if (HasFlag(usage, TextureUsage::CopyDestination))
                {
                    l_Flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                }

                return l_Flags;
            }

            VkImageViewType ToVkSampledViewType(TextureDimension dimension)
            {
                switch (dimension)
                {
                    case TextureDimension::Texture2DArray:
                    {
                        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                    }
                    case TextureDimension::TextureCube:
                    {
                        return VK_IMAGE_VIEW_TYPE_CUBE;
                    }
                    default:
                    {
                        return VK_IMAGE_VIEW_TYPE_2D;
                    }
                }
            }

            VkImageView CreateView(const VulkanDevice& device, VkImage image, Format format, VkImageViewType type, std::uint32_t baseMipLevel, std::uint32_t mipLevels, std::uint32_t baseArrayLayer, std::uint32_t arrayLayers)
            {
                VkImageViewCreateInfo l_Create = MakeInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
                l_Create.image = image;
                l_Create.viewType = type;
                l_Create.format = ToVkFormat(format);
                l_Create.subresourceRange = { GetAspect(format), baseMipLevel, mipLevels, baseArrayLayer, arrayLayers };

                VkImageView l_View = VK_NULL_HANDLE;
                const VkResult l_Result = vkCreateImageView(device.GetHandle(), &l_Create, nullptr, &l_View);
                if (l_Result != VK_SUCCESS)
                {
                    TR_CORE_ERROR("Vulkan: a view of {} mip(s) and {} layer(s) of a {} image could not be created ({})", mipLevels, arrayLayers, ToString(format), FormatResult(l_Result));

                    return VK_NULL_HANDLE;
                }

                return l_View;
            }
        }

        // The driver's optimal-tiling features for the format must cover every usage asked for
        bool VulkanTexture::IsFormatSupported(const VulkanDevice& device, Format format, TextureUsage usage, std::uint32_t sampleCount)
        {
            const VkFormat l_Format = ToVkFormat(format);
            if (l_Format == VK_FORMAT_UNDEFINED || (IsCompressedFormat(format) && !device.HasTextureCompressionBC()) || (HasStencil(format) && usage != TextureUsage::DepthStencil))
            {
                return false;
            }

            VkFormatProperties l_Properties{};
            vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDevice(), l_Format, &l_Properties);

            constexpr std::array<std::pair<TextureUsage, VkFormatFeatureFlags>, 6> c_Needs{ {
                { TextureUsage::ShaderResource, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
                { TextureUsage::UnorderedAccess, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT },
                { TextureUsage::RenderTarget, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT },
                { TextureUsage::DepthStencil, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT },
                { TextureUsage::CopySource, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT },
                { TextureUsage::CopyDestination, VK_FORMAT_FEATURE_TRANSFER_DST_BIT }
            } };

            if (!std::ranges::all_of(c_Needs, [usage, &l_Properties](const auto& need) { return !HasFlag(usage, need.first) || (l_Properties.optimalTilingFeatures & need.second) != 0; }))
            {
                return false;
            }

            if (sampleCount == 1)
            {
                return true;
            }

            const VkImageUsageFlags l_Usage = ToVkImageUsage(usage);
            VkImageFormatProperties l_ImageProperties{};
            if (l_Usage == 0 || vkGetPhysicalDeviceImageFormatProperties(device.GetPhysicalDevice(), l_Format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, l_Usage, 0, &l_ImageProperties) != VK_SUCCESS)
            {
                return false;
            }

            return (l_ImageProperties.sampleCounts & sampleCount) != 0;
        }

        std::optional<VulkanTexture> VulkanTexture::Create(const VulkanDevice& device, const VulkanAllocator& allocator, VulkanBindless& bindless, const TextureDescription& description)
        {
            VkImageCreateInfo l_Create = MakeInfo<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
            l_Create.imageType = VK_IMAGE_TYPE_2D;
            l_Create.format = ToVkFormat(description.TextureFormat);
            l_Create.flags = description.Dimension == TextureDimension::TextureCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
            l_Create.extent = { description.Width, description.Height, 1 };
            l_Create.mipLevels = description.MipLevels;
            l_Create.arrayLayers = description.ArrayLayers;
            l_Create.samples = static_cast<VkSampleCountFlagBits>(description.SampleCount);
            l_Create.tiling = VK_IMAGE_TILING_OPTIMAL;
            l_Create.usage = ToVkImageUsage(description.Usage);
            l_Create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            l_Create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            TR_CORE_ASSERT(l_Create.usage != 0, "Texture '{}' has no usage.", description.DebugName);

            VmaAllocationCreateInfo l_Allocation{};
            l_Allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

            VulkanTexture l_Texture;
            const VkResult l_Result = vmaCreateImage(allocator.GetHandle(), &l_Create, &l_Allocation, &l_Texture.Image, &l_Texture.Allocation, nullptr);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: texture '{}' ({}x{} {}) could not be created ({})", description.DebugName, description.Width, description.Height, ToString(description.TextureFormat), FormatResult(l_Result));

                return {};
            }

            l_Texture.TextureFormat = description.TextureFormat;
            l_Texture.Dimension = description.Dimension;
            l_Texture.Width = description.Width;
            l_Texture.Height = description.Height;
            l_Texture.MipLevels = description.MipLevels;
            l_Texture.ArrayLayers = description.ArrayLayers;
            l_Texture.SampleCount = description.SampleCount;
            device.SetDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(l_Texture.Image), description.DebugName);

            if (HasFlag(description.Usage, TextureUsage::RenderTarget) || HasFlag(description.Usage, TextureUsage::DepthStencil))
            {
                l_Texture.AttachmentViews.reserve(std::size_t{ description.MipLevels } * description.ArrayLayers);
                for (std::uint32_t it_Layer = 0; it_Layer < description.ArrayLayers; ++it_Layer)
                {
                    for (std::uint32_t it_Mip = 0; it_Mip < description.MipLevels; ++it_Mip)
                    {
                        l_Texture.AttachmentViews.push_back(CreateView(device, l_Texture.Image, description.TextureFormat, VK_IMAGE_VIEW_TYPE_2D, it_Mip, 1, it_Layer, 1));
                    }
                }
            }

            if (HasFlag(description.Usage, TextureUsage::ShaderResource))
            {
                l_Texture.SampledView = CreateView(device, l_Texture.Image, description.TextureFormat, ToVkSampledViewType(description.Dimension), 0, description.MipLevels, 0, description.ArrayLayers);
                l_Texture.ShaderResourceIndex = bindless.AddImageDescriptor(c_SampledImageBinding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, l_Texture.SampledView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }

            // Storage images are written one mip at a time, and a cube's faces as the layers of an array, since a storage view cannot be a cube
            if (HasFlag(description.Usage, TextureUsage::UnorderedAccess))
            {
                const VkImageViewType l_Type = description.Dimension == TextureDimension::Texture2D ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                l_Texture.StorageView = CreateView(device, l_Texture.Image, description.TextureFormat, l_Type, 0, 1, 0, description.ArrayLayers);
                l_Texture.UnorderedAccessIndex = bindless.AddImageDescriptor(c_StorageImageBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, l_Texture.StorageView, VK_IMAGE_LAYOUT_GENERAL);
            }

            return l_Texture;
        }

        VulkanTexture VulkanTexture::FromSwapChainImage(const VulkanDevice& device, VkImage image, Format format, std::uint32_t width, std::uint32_t height)
        {
            VulkanTexture l_Texture;
            l_Texture.Image = image;
            l_Texture.AttachmentViews.push_back(CreateView(device, image, format, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1));
            l_Texture.TextureFormat = format;
            l_Texture.Width = width;
            l_Texture.Height = height;
            l_Texture.MipLevels = 1;
            l_Texture.ArrayLayers = 1;

            return l_Texture;
        }

        VulkanRelease VulkanTexture::ToRelease() const
        {
            VulkanRelease l_Release;
            l_Release.Image = Image;
            l_Release.Views = AttachmentViews;
            if (StorageView != VK_NULL_HANDLE)
            {
                l_Release.Views.push_back(StorageView);
            }

            l_Release.Allocation = Allocation;
            l_Release.SampledView = SampledView;
            l_Release.ShaderResourceIndex = ShaderResourceIndex;
            l_Release.UnorderedAccessIndex = UnorderedAccessIndex;

            return l_Release;
        }

        void VulkanTexture::DestroySwapChainView(const VulkanDevice& device) const
        {
            vkDestroyImageView(device.GetHandle(), AttachmentViews.front(), nullptr);
        }
    }
}