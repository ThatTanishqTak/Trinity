#pragma once

#include <string>

namespace Trinity
{
    namespace CpuFeatures
    {
        // Comma-separated, empty when none are missing. Runs on any x64 processor, before any physics code
        [[nodiscard]] std::string GetMissingPhysicsInstructions();
    }
}