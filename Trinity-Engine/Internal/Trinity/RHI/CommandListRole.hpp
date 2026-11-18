#pragma once

#include <cstdint>

namespace Trinity
{
    namespace RHI
    {
        enum class CommandListRole : std::uint8_t
        {
            Frame,
            Uploads,
            Compute
        };
    }
}