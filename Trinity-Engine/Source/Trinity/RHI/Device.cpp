#include "Trinity/RHI/Device.hpp"

#include "Trinity/RHI/Null/NullDevice.hpp"

#if defined(TR_RHI_D3D12)
#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#endif

#include <format>
#include <utility>

namespace Trinity
{
    namespace RHI
    {
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
                case GraphicsAPI::Metal:
                {
                    break;
                }
            }

            return Unexpected{ std::format("there is no {} backend in this build", ToString(specification.API)) };
        }
    }
}