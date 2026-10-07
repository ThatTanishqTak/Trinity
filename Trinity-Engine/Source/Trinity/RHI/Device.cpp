#include "Trinity/RHI/Device.hpp"

#include "Trinity/RHI/Null/NullDevice.hpp"

#if defined(TR_RHI_D3D12)
#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#endif

#if defined(TR_RHI_VULKAN)
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#endif

#include "Trinity/Core/Log.hpp"

#include <format>
#include <utility>

namespace Trinity
{
    namespace RHI
    {
        UploadAllocation Device::AllocateUpload(std::uint64_t size, std::uint64_t alignment)
        {
            return m_UploadRing.Allocate(size, alignment);
        }

        std::uint64_t Device::GetUploadCapacity() const
        {
            return m_UploadRing.GetCapacity();
        }

        bool Device::CanCreateTexture(const TextureDescription& description) const
        {
            if (!IsFormatSupported(description.TextureFormat, description.Usage))
            {
                TR_CORE_ERROR("{}: this device cannot create texture '{}' as {} with the usage it asks for", ToString(GetInfo().API), description.DebugName, ToString(description.TextureFormat));

                return false;
            }

            const std::uint32_t l_Block = GetFormatBlockDimension(description.TextureFormat);
            if (description.Width % l_Block != 0 || description.Height % l_Block != 0)
            {
                TR_CORE_ERROR("{}: texture '{}' is {}x{}, and a {} texture's size must be a multiple of {}", ToString(GetInfo().API), description.DebugName, description.Width, description.Height, ToString(description.TextureFormat), l_Block);

                return false;
            }

            const bool l_Layers = description.Dimension == TextureDimension::Texture2D ? description.ArrayLayers == 1 : description.Dimension == TextureDimension::TextureCube ? description.ArrayLayers == c_CubeFaceCount : description.ArrayLayers != 0 && description.ArrayLayers <= c_MaxArrayLayers;
            if (!l_Layers || (description.Dimension == TextureDimension::TextureCube && description.Width != description.Height))
            {
                TR_CORE_ERROR("{}: texture '{}' is {}x{} with {} layer(s), and a 2D texture has 1 layer, a cube {} layers and a square size, and an array 1 to {}", ToString(GetInfo().API), description.DebugName, description.Width, description.Height, description.ArrayLayers, c_CubeFaceCount, c_MaxArrayLayers);

                return false;
            }

            return true;
        }

        Expected<Scope<Device>, std::string> CreateDevice(const DeviceSpecification& specification)
        {
            switch (specification.API)
            {
                case GraphicsAPI::None:
                {
                    return Scope<Device>(CreateScope<NullDevice>(specification));
                }
                case GraphicsAPI::D3D12:
                {
#if defined(TR_RHI_D3D12)
                    std::string l_Error;
                    if (Scope<D3D12Device> l_Device = D3D12Device::Create(specification, l_Error))
                    {
                        return Scope<Device>(std::move(l_Device));
                    }

                    return Unexpected{ std::move(l_Error) };
#else
                    break;
#endif
                }
                case GraphicsAPI::Vulkan:
                {
#if defined(TR_RHI_VULKAN)
                    std::string l_Error;
                    if (Scope<VulkanDevice> l_Device = VulkanDevice::Create(specification, l_Error))
                    {
                        return Scope<Device>(std::move(l_Device));
                    }

                    return Unexpected{ std::move(l_Error) };
#else
                    break;
#endif
                }
                case GraphicsAPI::Metal:
                {
                    break;
                }
            }

            return Unexpected{ std::format("there is no {} backend in this build", ToString(specification.API)) };
        }
    }
}