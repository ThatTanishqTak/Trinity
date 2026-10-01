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
#if defined(TR_RHI_METAL)
                return true;
#else
                return false;
#endif
            }
        }
        return false;
    }

    GraphicsAPI GetDefaultGraphicsAPI()
    {
        for (const GraphicsAPI l_API : {GraphicsAPI::D3D12, GraphicsAPI::Metal, GraphicsAPI::Vulkan})
        {
            if (IsGraphicsAPIAvailable(l_API))
            {
                return l_API;
            }
        }
        return GraphicsAPI::None;
    }
}