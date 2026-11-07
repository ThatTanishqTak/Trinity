#pragma once

#include "Trinity/Core/Export.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Trinity
{
    namespace Hash
    {
        // XXH64, as xxHash's specification defines it, so a pack's hashes can be checked by any tool that has it
        [[nodiscard]] TRINITY_API std::uint64_t XXHash64(std::span<const std::byte> data, std::uint64_t seed = 0);

        // CRC-32 as zip, gzip and PNG compute it, continuing from the CRC of what came before, so data can be fed in pieces. 0 begins
        [[nodiscard]] TRINITY_API std::uint32_t Crc32(std::span<const std::byte> data, std::uint32_t crc = 0);

        // SHA-256 as FIPS 180-4 defines it, fed in pieces of any size, as a version's .sha256 holds it for its zip
        class TRINITY_API Sha256
        {
        public:
            static constexpr std::size_t c_DigestSize = 32;
            using Digest = std::array<std::uint8_t, c_DigestSize>;

            Sha256();

            void Update(std::span<const std::byte> data);
            // The digest of everything fed since it began, after which it begins again
            [[nodiscard]] Digest Finish();

            // In lowercase, as sha256sum and CMake's file(SHA256) write it
            [[nodiscard]] static std::string ToHex(const Digest& digest);

        private:
            void Reset();
            void Compress(const std::byte* block);

            std::array<std::uint32_t, 8> m_State{};
            std::array<std::byte, 64> m_Block{};
            std::size_t m_BlockSize = 0;
            std::uint64_t m_Length = 0;
        };
    }
}