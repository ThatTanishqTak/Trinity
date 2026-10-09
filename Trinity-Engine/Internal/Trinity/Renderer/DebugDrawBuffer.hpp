#pragma once

#include "Trinity/Renderer/DebugDraw.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>

#if TR_DEBUG_DRAW

namespace Trinity
{
    // One end of a debug line, as DebugLines.slang reads it: a world position, and an sRGB colour with red in the low byte
    struct DebugVertex
    {
        glm::vec3 Position{ 0.0f };
        std::uint32_t Color = 0;
    };

    static_assert(sizeof(DebugVertex) == 16);

    // The frame's debug lines, two vertices a line, as the Renderer reads them, then clears them once the frame's graph is built
    namespace DebugDrawBuffer
    {
        [[nodiscard]] std::span<const DebugVertex> GetVertices(DebugDepth depth);
        void Clear();
        // Frees the lists' memory as the Renderer is destroyed, so Renderer is 0 B at the leak report
        void Release();
    }
}

#endif