#include "Trinity/Core/Hash.hpp"

#include <bit>
#include <cstring>

namespace Trinity
{
    namespace
    {
        constexpr std::uint64_t c_Prime1 = 0x9E3779B185EBCA87ull;
        constexpr std::uint64_t c_Prime2 = 0xC2B2AE3D27D4EB4Full;
        constexpr std::uint64_t c_Prime3 = 0x165667B19E3779F9ull;
        constexpr std::uint64_t c_Prime4 = 0x85EBCA77C2B2AE63ull;
        constexpr std::uint64_t c_Prime5 = 0x27D4EB2F165667C5ull;

        // The specification reads its input as little-endian words
        template<typename T>
        T ReadLittleEndian(const std::byte* data)
        {
            T l_Value{};
            std::memcpy(&l_Value, data, sizeof(T));
            if constexpr (std::endian::native == std::endian::big)
            {
                l_Value = std::byteswap(l_Value);
            }

            return l_Value;
        }

        std::uint64_t Round(std::uint64_t accumulator, std::uint64_t input)
        {
            accumulator += input * c_Prime2;
            accumulator = std::rotl(accumulator, 31);

            return accumulator * c_Prime1;
        }

        std::uint64_t MergeRound(std::uint64_t accumulator, std::uint64_t value)
        {
            accumulator ^= Round(0, value);

            return accumulator * c_Prime1 + c_Prime4;
        }
    }

    namespace Hash
    {
        std::uint64_t XXHash64(std::span<const std::byte> data, std::uint64_t seed)
        {
            const std::byte* l_Position = data.data();
            const std::byte* const l_End = l_Position + data.size();

            std::uint64_t l_Hash = 0;
            if (data.size() >= 32)
            {
                std::uint64_t l_Lane1 = seed + c_Prime1 + c_Prime2;
                std::uint64_t l_Lane2 = seed + c_Prime2;
                std::uint64_t l_Lane3 = seed;
                std::uint64_t l_Lane4 = seed - c_Prime1;

                for (; l_End - l_Position >= 32; l_Position += 32)
                {
                    l_Lane1 = Round(l_Lane1, ReadLittleEndian<std::uint64_t>(l_Position));
                    l_Lane2 = Round(l_Lane2, ReadLittleEndian<std::uint64_t>(l_Position + 8));
                    l_Lane3 = Round(l_Lane3, ReadLittleEndian<std::uint64_t>(l_Position + 16));
                    l_Lane4 = Round(l_Lane4, ReadLittleEndian<std::uint64_t>(l_Position + 24));
                }

                l_Hash = std::rotl(l_Lane1, 1) + std::rotl(l_Lane2, 7) + std::rotl(l_Lane3, 12) + std::rotl(l_Lane4, 18);
                l_Hash = MergeRound(l_Hash, l_Lane1);
                l_Hash = MergeRound(l_Hash, l_Lane2);
                l_Hash = MergeRound(l_Hash, l_Lane3);
                l_Hash = MergeRound(l_Hash, l_Lane4);
            }
            else
            {
                l_Hash = seed + c_Prime5;
            }

            l_Hash += static_cast<std::uint64_t>(data.size());

            for (; l_End - l_Position >= 8; l_Position += 8)
            {
                l_Hash ^= Round(0, ReadLittleEndian<std::uint64_t>(l_Position));
                l_Hash = std::rotl(l_Hash, 27) * c_Prime1 + c_Prime4;
            }

            if (l_End - l_Position >= 4)
            {
                l_Hash ^= static_cast<std::uint64_t>(ReadLittleEndian<std::uint32_t>(l_Position)) * c_Prime1;
                l_Hash = std::rotl(l_Hash, 23) * c_Prime2 + c_Prime3;
                l_Position += 4;
            }

            for (; l_Position < l_End; ++l_Position)
            {
                l_Hash ^= std::to_integer<std::uint64_t>(*l_Position) * c_Prime5;
                l_Hash = std::rotl(l_Hash, 11) * c_Prime1;
            }

            l_Hash ^= l_Hash >> 33;
            l_Hash *= c_Prime2;
            l_Hash ^= l_Hash >> 29;
            l_Hash *= c_Prime3;
            l_Hash ^= l_Hash >> 32;

            return l_Hash;
        }
    }
}