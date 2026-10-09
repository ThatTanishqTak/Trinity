#pragma once

#include <string>

namespace Trinity
{
    namespace CpuFeatures
    {
        // The instructions the physics libraries are built for that this processor, or its operating system, does not support, by name and apart by commas. Empty when it supports them all, or on a processor that is not x64. Built for any x64 processor, so it runs safely before any physics code
        [[nodiscard]] std::string GetMissingPhysicsInstructions();
    }
}