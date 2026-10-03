#pragma once

#include "Trinity/Core/Export.hpp"

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

    [[nodiscard]] TRINITY_API std::string_view ToString(GraphicsAPI api);

    [[nodiscard]] TRINITY_API bool IsGraphicsAPIAvailable(GraphicsAPI api);

    [[nodiscard]] TRINITY_API GraphicsAPI GetDefaultGraphicsAPI();
}