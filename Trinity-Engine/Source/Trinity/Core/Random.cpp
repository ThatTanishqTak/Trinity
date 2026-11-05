#include "Trinity/Core/Random.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <random>
#include <utility>

namespace Trinity
{
    namespace
    {
        using State = std::array<std::uint32_t, 4>;

        // Each 64-bit step of SplitMix64 fills two words, so even a seed of 0 gives a state that is not all zero
        State Spread(std::uint64_t seed)
        {
            State l_State{};
            for (std::size_t it_Word = 0; it_Word < l_State.size(); it_Word += 2)
            {
                seed += 0x9E3779B97F4A7C15ull;
                std::uint64_t l_Mixed = seed;
                l_Mixed = (l_Mixed ^ (l_Mixed >> 30)) * 0xBF58476D1CE4E5B9ull;
                l_Mixed = (l_Mixed ^ (l_Mixed >> 27)) * 0x94D049BB133111EBull;
                l_Mixed ^= l_Mixed >> 31;
                l_State[it_Word] = static_cast<std::uint32_t>(l_Mixed);
                l_State[it_Word + 1] = static_cast<std::uint32_t>(l_Mixed >> 32);
            }

            return l_State;
        }

        State& GetState()
        {
            static State s_State = []
            {
                std::random_device l_Device;

                return Spread((static_cast<std::uint64_t>(l_Device()) << 32) | l_Device());
            }();

            return s_State;
        }
    }

    void Random::InitState(std::int32_t seed)
    {
        GetState() = Spread(static_cast<std::uint32_t>(seed));
    }

    // xoshiro128**, as Blackman and Vigna give it
    std::uint32_t Random::NextUInt()
    {
        State& l_State = GetState();
        const std::uint32_t l_Result = std::rotl(l_State[1] * 5u, 7) * 9u;
        const std::uint32_t l_Shifted = l_State[1] << 9;
        l_State[2] ^= l_State[0];
        l_State[3] ^= l_State[1];
        l_State[1] ^= l_State[2];
        l_State[0] ^= l_State[3];
        l_State[2] ^= l_Shifted;
        l_State[3] = std::rotl(l_State[3], 11);

        return l_Result;
    }

    // The top 24 bits, which a float holds exactly, over the largest of them, so both 0 and 1 come up
    float Random::Value()
    {
        return static_cast<float>(NextUInt() >> 8) / 16777215.0f;
    }

    // Held to the range, as the sum may round past either end
    float Random::Range(float min, float max)
    {
        if (max < min)
        {
            std::swap(min, max);
        }

        return std::clamp(min + (max - min) * Value(), min, max);
    }

    // Lemire's multiply and shift, drawing again for the few values that would make some results likelier than others
    std::int32_t Random::Range(std::int32_t min, std::int32_t max)
    {
        if (max <= min)
        {
            return min;
        }

        const std::uint32_t l_Span = static_cast<std::uint32_t>(static_cast<std::int64_t>(max) - static_cast<std::int64_t>(min));
        std::uint64_t l_Product = static_cast<std::uint64_t>(NextUInt()) * l_Span;
        if (static_cast<std::uint32_t>(l_Product) < l_Span)
        {
            const std::uint32_t l_Threshold = (0u - l_Span) % l_Span;
            while (static_cast<std::uint32_t>(l_Product) < l_Threshold)
            {
                l_Product = static_cast<std::uint64_t>(NextUInt()) * l_Span;
            }
        }

        return static_cast<std::int32_t>(static_cast<std::int64_t>(min) + static_cast<std::int64_t>(l_Product >> 32));
    }
}