#include "Trinity/Core/Hash.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>

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

        // The reflected polynomial zip uses, one entry for each byte
        constexpr std::array<std::uint32_t, 256> MakeCrcTable()
        {
            std::array<std::uint32_t, 256> l_Table{};
            for (std::uint32_t it_Byte = 0; it_Byte < 256; ++it_Byte)
            {
                std::uint32_t l_Value = it_Byte;
                for (int it_Bit = 0; it_Bit < 8; ++it_Bit)
                {
                    l_Value = (l_Value & 1u) != 0 ? 0xEDB88320u ^ (l_Value >> 1) : l_Value >> 1;
                }

                l_Table[it_Byte] = l_Value;
            }

            return l_Table;
        }

        constexpr std::array<std::uint32_t, 256> c_CrcTable = MakeCrcTable();

        // The first 32 bits of the fractional parts of the cube roots of the first 64 primes, and of the square roots of the first 8, as FIPS 180-4 gives them
        constexpr std::array<std::uint32_t, 64> c_ShaRounds
        {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };

        constexpr std::array<std::uint32_t, 8> c_ShaStart{ 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };

        // SHA-256 reads its words big-endian
        std::uint32_t ReadBigEndian(const std::byte* data)
        {
            return (std::to_integer<std::uint32_t>(data[0]) << 24) | (std::to_integer<std::uint32_t>(data[1]) << 16) | (std::to_integer<std::uint32_t>(data[2]) << 8) | std::to_integer<std::uint32_t>(data[3]);
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

        std::uint32_t Crc32(std::span<const std::byte> data, std::uint32_t crc)
        {
            std::uint32_t l_Crc = ~crc;
            for (const std::byte it_Byte : data)
            {
                l_Crc = c_CrcTable[(l_Crc ^ std::to_integer<std::uint32_t>(it_Byte)) & 0xFFu] ^ (l_Crc >> 8);
            }

            return ~l_Crc;
        }

        Sha256::Sha256()
        {
            Reset();
        }

        void Sha256::Update(std::span<const std::byte> data)
        {
            m_Length += data.size();
            std::size_t l_Offset = 0;
            if (m_BlockSize > 0)
            {
                const std::size_t l_Taken = std::min(data.size(), m_Block.size() - m_BlockSize);
                std::memcpy(m_Block.data() + m_BlockSize, data.data(), l_Taken);
                m_BlockSize += l_Taken;
                l_Offset = l_Taken;
                if (m_BlockSize < m_Block.size())
                {
                    return;
                }

                Compress(m_Block.data());
                m_BlockSize = 0;
            }

            for (; l_Offset + m_Block.size() <= data.size(); l_Offset += m_Block.size())
            {
                Compress(data.data() + l_Offset);
            }

            m_BlockSize = data.size() - l_Offset;
            if (m_BlockSize > 0)
            {
                std::memcpy(m_Block.data(), data.data() + l_Offset, m_BlockSize);
            }
        }

        // Padded with a 1 bit, then zeros, then the length in bits as 64 bits big-endian, which ends a block
        Sha256::Digest Sha256::Finish()
        {
            const std::uint64_t l_Bits = m_Length * 8;
            m_Block[m_BlockSize++] = std::byte{ 0x80 };
            if (m_BlockSize > m_Block.size() - 8)
            {
                std::memset(m_Block.data() + m_BlockSize, 0, m_Block.size() - m_BlockSize);
                Compress(m_Block.data());
                m_BlockSize = 0;
            }

            std::memset(m_Block.data() + m_BlockSize, 0, m_Block.size() - 8 - m_BlockSize);
            for (std::size_t it_Byte = 0; it_Byte < 8; ++it_Byte)
            {
                m_Block[m_Block.size() - 1 - it_Byte] = static_cast<std::byte>((l_Bits >> (it_Byte * 8)) & 0xFFu);
            }

            Compress(m_Block.data());

            Digest l_Digest{};
            for (std::size_t it_Word = 0; it_Word < m_State.size(); ++it_Word)
            {
                for (std::size_t it_Byte = 0; it_Byte < 4; ++it_Byte)
                {
                    l_Digest[it_Word * 4 + it_Byte] = static_cast<std::uint8_t>((m_State[it_Word] >> (24 - it_Byte * 8)) & 0xFFu);
                }
            }

            Reset();

            return l_Digest;
        }

        std::string Sha256::ToHex(const Digest& digest)
        {
            std::string l_Text;
            l_Text.reserve(digest.size() * 2);
            for (const std::uint8_t it_Byte : digest)
            {
                l_Text += std::format("{:02x}", it_Byte);
            }

            return l_Text;
        }

        void Sha256::Reset()
        {
            m_State = c_ShaStart;
            m_BlockSize = 0;
            m_Length = 0;
        }

        void Sha256::Compress(const std::byte* block)
        {
            std::array<std::uint32_t, 64> l_Words{};
            for (std::size_t it_Word = 0; it_Word < 16; ++it_Word)
            {
                l_Words[it_Word] = ReadBigEndian(block + it_Word * 4);
            }

            for (std::size_t it_Word = 16; it_Word < l_Words.size(); ++it_Word)
            {
                const std::uint32_t l_Sigma0 = std::rotr(l_Words[it_Word - 15], 7) ^ std::rotr(l_Words[it_Word - 15], 18) ^ (l_Words[it_Word - 15] >> 3);
                const std::uint32_t l_Sigma1 = std::rotr(l_Words[it_Word - 2], 17) ^ std::rotr(l_Words[it_Word - 2], 19) ^ (l_Words[it_Word - 2] >> 10);
                l_Words[it_Word] = l_Words[it_Word - 16] + l_Sigma0 + l_Words[it_Word - 7] + l_Sigma1;
            }

            std::array<std::uint32_t, 8> l_Working = m_State;
            for (std::size_t it_Round = 0; it_Round < c_ShaRounds.size(); ++it_Round)
            {
                const std::uint32_t l_Sum1 = std::rotr(l_Working[4], 6) ^ std::rotr(l_Working[4], 11) ^ std::rotr(l_Working[4], 25);
                const std::uint32_t l_Choice = (l_Working[4] & l_Working[5]) ^ (~l_Working[4] & l_Working[6]);
                const std::uint32_t l_First = l_Working[7] + l_Sum1 + l_Choice + c_ShaRounds[it_Round] + l_Words[it_Round];
                const std::uint32_t l_Sum0 = std::rotr(l_Working[0], 2) ^ std::rotr(l_Working[0], 13) ^ std::rotr(l_Working[0], 22);
                const std::uint32_t l_Majority = (l_Working[0] & l_Working[1]) ^ (l_Working[0] & l_Working[2]) ^ (l_Working[1] & l_Working[2]);
                const std::uint32_t l_Second = l_Sum0 + l_Majority;

                l_Working[7] = l_Working[6];
                l_Working[6] = l_Working[5];
                l_Working[5] = l_Working[4];
                l_Working[4] = l_Working[3] + l_First;
                l_Working[3] = l_Working[2];
                l_Working[2] = l_Working[1];
                l_Working[1] = l_Working[0];
                l_Working[0] = l_First + l_Second;
            }

            for (std::size_t it_Word = 0; it_Word < m_State.size(); ++it_Word)
            {
                m_State[it_Word] += l_Working[it_Word];
            }
        }
    }
}