#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // One file or folder of a zip, as its central directory names it
    struct ZipEntry
    {
        // As the zip holds it, with / between folders, and a folder's ending in /
        std::string Name;
        std::uint64_t Size = 0;
        std::uint64_t CompressedSize = 0;
        std::uint32_t Crc = 0;
        // 0 when stored as it is, 8 when deflated
        std::uint16_t Method = 0;
        // The Unix mode it was zipped with, its type and permissions, or 0 when it was zipped where there is none
        std::uint32_t UnixMode = 0;
        std::uint64_t LocalHeaderOffset = 0;

        [[nodiscard]] bool IsDirectory() const { return Name.ends_with('/'); }
        [[nodiscard]] bool IsLink() const { return (UnixMode & 0170000u) == 0120000u; }
    };

    // A zip read from a file on the disk, as Hub reads an engine version's: its entries from its central directory, and each entry's bytes, inflated or as stored, handed on in pieces with its size and CRC-32 checked. It reads what zip tools and bsdtar write, stored and deflated entries with or without data descriptors. Zip64, which holds entries of 4 GB or more or more than 65535 of them, spanned and encrypted zips, and other methods are refused, naming why
    class TRINITY_API ZipArchive
    {
    public:
        // Each piece of an entry in order. False stops it, and Extract then fails
        using Sink = std::function<bool(std::span<const std::byte>)>;

        ZipArchive() = default;

        ZipArchive(ZipArchive&&) = default;
        ZipArchive& operator=(ZipArchive&&) = default;

        ZipArchive(const ZipArchive&) = delete;
        ZipArchive& operator=(const ZipArchive&) = delete;

        // The zip's entries, without reading their bytes. Refused, naming why, for a file that cannot be read, is no zip, or is one this reader does not read
        [[nodiscard]] static Expected<ZipArchive, std::string> Open(const std::filesystem::path& path);

        [[nodiscard]] const std::vector<ZipEntry>& GetEntries() const { return m_Entries; }
        [[nodiscard]] const ZipEntry* Find(std::string_view name) const;

        // The entry's bytes to sink, in pieces of at most 64 KB. Fails, naming why, for an entry whose bytes are damaged, cut short or not the size and CRC-32 the zip gives, or when sink stops it
        [[nodiscard]] Expected<void, std::string> Extract(const ZipEntry& entry, const Sink& sink);
        // A small entry whole, such as a manifest, refused when it is larger than limit
        [[nodiscard]] Expected<std::string, std::string> ReadText(const ZipEntry& entry, std::size_t limit = 1024 * 1024);

    private:
        std::ifstream m_File;
        std::uint64_t m_DirectoryOffset = 0;
        std::vector<ZipEntry> m_Entries;
    };
}