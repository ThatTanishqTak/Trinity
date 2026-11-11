#include "Trinity/RHI/Vulkan/VulkanSampler.hpp"
#include "Trinity/RHI/Vulkan/VulkanBindless.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Log.hpp"

#include <optional>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            VkFilter ToVkFilter(Filter filter)
            {
                return filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            }

            VkSamplerAddressMode ToVkAddressMode(AddressMode address)
            {
                switch (address)
                {
                    case AddressMode::MirroredRepeat:
                    {
                        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                    }
                    case AddressMode::ClampToEdge:
                    {
                        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                    }
                    default:
                    {
                        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    }
                }
            }
        }

        // Shaders read samplers from their own array at c_SamplerBinding, indexed separately from resources
        std::optional<VulkanSampler> VulkanSampler::Create(const VulkanDevice& device, VulkanBindless& bindless, const SamplerDescription& description)
        {
            VkSamplerCreateInfo l_Create = MakeInfo<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
            l_Create.magFilter = ToVkFilter(description.MagFilter);
            l_Create.minFilter = ToVkFilter(description.MinFilter);
            l_Create.mipmapMode = description.MipFilter == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
            l_Create.addressModeU = ToVkAddressMode(description.AddressU);
            l_Create.addressModeV = ToVkAddressMode(description.AddressV);
            l_Create.addressModeW = ToVkAddressMode(description.AddressW);
            l_Create.mipLodBias = description.MipLodBias;
            l_Create.minLod = description.MinLod;
            l_Create.maxLod = description.MaxLod >= c_LodUnclamped ? VK_LOD_CLAMP_NONE : description.MaxLod;
            l_Create.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

            VulkanSampler l_Sampler;
            const VkResult l_Result = vkCreateSampler(device.GetHandle(), &l_Create, nullptr, &l_Sampler.Sampler);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: sampler '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            l_Sampler.Index = bindless.AddSamplerDescriptor(l_Sampler.Sampler);
            if (l_Sampler.Index == c_NoBindlessIndex)
            {
                TR_CORE_ERROR("Vulkan: sampler '{}' could not be created, since all {} bindless sampler indices are in use", description.DebugName, c_BindlessSamplerCapacity);
                vkDestroySampler(device.GetHandle(), l_Sampler.Sampler, nullptr);

                return {};
            }

            device.SetDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(l_Sampler.Sampler), description.DebugName);

            return l_Sampler;
        }

        VulkanRelease VulkanSampler::ToRelease() const
        {
            VulkanRelease l_Release;
            l_Release.Sampler = Sampler;
            l_Release.SamplerIndex = Index;

            return l_Release;
        }
    }
}