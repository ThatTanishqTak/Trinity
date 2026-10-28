#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace Trinity
{
    static_assert(std::endian::native == std::endian::little, "Packs are written and read as little-endian, as every platform Trinity targets is.");

    enum class PackCompression : std::uint32_t
    {
        None,
        Zstd
    };

    // A .trpak: this header, then each file's bytes, then the table. The table is an entry for each file, sorted by path, then the paths as UTF-8 one after another, relative to where the pack mounts
    struct PackHeader
    {
        static constexpr std::array<char, 8> c_Magic{ 'T', 'R', 'P', 'A', 'K', '\0', '\0', '\0' };
        static constexpr std::uint32_t c_Version = 1;

        std::array<char, 8> Magic = c_Magic;
        std::uint32_t Version = c_Version;
        std::uint32_t FileCount = 0;
        // The whole pack's size, so one that was cut short is refused as it mounts
        std::uint64_t PackSize = 0;
        std::uint64_t TableOffset = 0;
        std::uint64_t TableSize = 0;
        // XXH64 of the table
        std::uint64_t TableHash = 0;
    };

    static_assert(sizeof(PackHeader) == 48);

    struct PackEntry
    {
        // Where the file's bytes start, from the start of the pack, and how many of them the pack holds
        std::uint64_t Offset = 0;
        std::uint64_t StoredSize = 0;
        // The file's size once read, which StoredSize equals unless it is compressed
        std::uint64_t Size = 0;
        // XXH64 of the file as it is read, after decompressing
        std::uint64_t Hash = 0;
        // Where its path starts among the paths after the entries
        std::uint32_t PathOffset = 0;
        std::uint32_t PathSize = 0;
        PackCompression Compression = PackCompression::None;
        std::uint32_t Padding = 0;
    };

    static_assert(sizeof(PackEntry) == 48);

    namespace PackLayout
    {
        // Files over 64 KiB start on a 4 KiB page, for the reads streamed from them, and smaller ones are packed tight
        inline constexpr std::uint64_t c_PageSize = 4096;
        inline constexpr std::uint64_t c_PageAlignedAbove = 64 * 1024;

        [[nodiscard]] constexpr std::uint64_t GetAlignment(std::uint64_t storedSize)
        {
            return storedSize > c_PageAlignedAbove ? c_PageSize : 1;
        }

        // Compressed only where that saves a tenth of the file or more, since KTX2 textures and audio clips are compressed already
        [[nodiscard]] constexpr bool IsWorthCompressing(std::uint64_t size, std::uint64_t compressedSize)
        {
            return compressedSize * 10 <= size * 9;
        }
    }
}