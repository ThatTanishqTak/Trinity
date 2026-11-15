#pragma once

#include <cstdint>

namespace Trinity
{
    namespace RHI
    {
        // What a backend command list records: the frame's commands on the graphics queue, the loaders' uploads for the copy queue, or the passes on the compute queue
        enum class CommandListRole : std::uint8_t
        {
            Frame,
            Uploads,
            Compute
        };
    }
}