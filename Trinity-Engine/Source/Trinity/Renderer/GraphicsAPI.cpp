#include "Trinity/Renderer/GraphicsAPI.hpp"

namespace Trinity
{
    std::string_view ToString(GraphicsAPI api)
    {
        switch (api)
        {
            case GraphicsAPI::None:
            {
                return "None";
            }
            case GraphicsAPI::D3D12:
            {
                return "D3D12";
            }
            case GraphicsAPI::Vulkan:
            {
                return "Vulkan";
            }
            case GraphicsAPI::Metal:
            {
                return "Metal";
            }
        }

        return "Unknown";
    }

    bool IsGraphicsAPIAvailable(GraphicsAPI api)
    {
        switch (api)
        {
            case GraphicsAPI::None:
            {
                return true;
            }
            case GraphicsAPI::D3D12:
            {
#if defined(TR_RHI_D3D12)
                return true;
#else
                return false;
#endif
            }
            case GraphicsAPI::Vulkan:
            {
#if defined(TR_RHI_VULKAN)
                return true;
#else
                return false;
#endif
            }
            case GraphicsAPI::Metal:
            {
                return false;
            }
        }
        return false;
    }

    GraphicsAPI GetDefaultGraphicsAPI()
    {
        return GraphicsAPI::None;
    }
}