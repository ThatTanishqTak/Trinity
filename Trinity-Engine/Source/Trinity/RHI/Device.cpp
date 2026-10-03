#include "Trinity/RHI/Device.hpp"

#include "Trinity/RHI/Null/NullDevice.hpp"

#include <format>

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
                case GraphicsAPI::Vulkan:
                case GraphicsAPI::Metal:
                {
                    break;
                }
            }

            return Unexpected{ std::format("there is no {} backend yet", ToString(specification.API)) };
        }
    }
}