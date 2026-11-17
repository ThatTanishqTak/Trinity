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
        // XXH64 as xxHash's specification defines it
        [[nodiscard]] TRINITY_API std::uint64_t XXHash64(std::span<const std::byte> data, std::uint64_t seed = 0);

        // As zip, gzip and PNG compute it. Pass the CRC of what came before to continue, 0 to begin
        [[nodiscard]] TRINITY_API std::uint32_t Crc32(std::span<const std::byte> data, std::uint32_t crc = 0);

        class TRINITY_API Sha256
        {
        public:
            static constexpr std::size_t c_DigestSize = 32;
            using Digest = std::array<std::uint8_t, c_DigestSize>;

            Sha256();

            void Update(std::span<const std::byte> data);
            // Begins a new digest afterwards
            [[nodiscard]] Digest Finish();

            // Lowercase, as sha256sum writes it
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