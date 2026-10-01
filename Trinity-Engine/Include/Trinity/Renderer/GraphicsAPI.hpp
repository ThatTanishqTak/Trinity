#pragma once

#include <cstdint>
#include <string_view>

namespace Trinity
{
    enum class GraphicsAPI : std::uint8_t
    {
        None = 0,
        D3D12,
        Vulkan,
        Metal
    };

    [[nodiscard]] std::string_view ToString(GraphicsAPI api);

    [[nodiscard]] bool IsGraphicsAPIAvailable(GraphicsAPI api);

    [[nodiscard]] GraphicsAPI GetDefaultGraphicsAPI();
}