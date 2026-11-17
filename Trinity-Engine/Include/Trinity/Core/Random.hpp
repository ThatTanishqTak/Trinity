#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstdint>

namespace Trinity
{
    // xoshiro128** seeded by SplitMix64: a seed gives the same numbers in C++ and C#. Main thread only
    class TRINITY_API Random
    {
    public:
        Random() = delete;

        static void InitState(std::int32_t seed);
        // 0 to 1, both included
        [[nodiscard]] static float Value();
        // Both ends included, and swapped when max is below min
        [[nodiscard]] static float Range(float min, float max);
        // max is excluded. Gives min when max is not above it
        [[nodiscard]] static std::int32_t Range(std::int32_t min, std::int32_t max);
        [[nodiscard]] static std::uint32_t NextUInt();
    };
}