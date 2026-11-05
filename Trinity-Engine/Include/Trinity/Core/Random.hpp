#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstdint>

namespace Trinity
{
    // As Unity's Random: one generator for the whole game, xoshiro128** under a seed SplitMix64 spreads over its state, so a seed gives the same numbers on every machine, in C++ and in C#, which reaches this one. Seeded from the system's own randomness until InitState is called. Main thread only
    class TRINITY_API Random
    {
    public:
        Random() = delete;

        static void InitState(std::int32_t seed);
        // From 0 to 1, both of them included, as Unity's value is, in steps of 1 / (2^24 - 1)
        [[nodiscard]] static float Value();
        // From min to max, both of them included, the two swapped when max is below min
        [[nodiscard]] static float Range(float min, float max);
        // From min up to but not including max, each as likely as the others, or min itself when max is not above it
        [[nodiscard]] static std::int32_t Range(std::int32_t min, std::int32_t max);
        // The next 32 bits as they come
        [[nodiscard]] static std::uint32_t NextUInt();
    };
}