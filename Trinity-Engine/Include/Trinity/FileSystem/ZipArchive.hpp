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
    struct ZipEntry
    {
        // With / between folders, and a folder's name ending in /
        std::string Name;
        std::uint64_t Size = 0;
        std::uint64_t CompressedSize = 0;
        std::uint32_t Crc = 0;
        // 0 when stored as it is, 8 when deflated
        std::uint16_t Method = 0;
        // Type and permissions, or 0 when it was zipped where there are none
        std::uint32_t UnixMode = 0;
        std::uint64_t LocalHeaderOffset = 0;

        [[nodiscard]] bool IsDirectory() const { return Name.ends_with('/'); }
        [[nodiscard]] bool IsLink() const { return (UnixMode & 0170000u) == 0120000u; }
    };

    // Reads stored and deflated entries. Zip64, spanned and encrypted zips are refused, naming why
    class TRINITY_API ZipArchive
    {
    public:
        // Called with each piece in order. False stops it, and Extract then fails
        using Sink = std::function<bool(std::span<const std::byte>)>;

        ZipArchive() = default;

        ZipArchive(ZipArchive&&) = default;
        ZipArchive& operator=(ZipArchive&&) = default;

        ZipArchive(const ZipArchive&) = delete;
        ZipArchive& operator=(const ZipArchive&) = delete;

        // Reads the entries without their bytes
        [[nodiscard]] static Expected<ZipArchive, std::string> Open(const std::filesystem::path& path);

        [[nodiscard]] const std::vector<ZipEntry>& GetEntries() const { return m_Entries; }
        [[nodiscard]] const ZipEntry* Find(std::string_view name) const;

        // Pieces are at most 64 KB. Fails when the bytes do not match the zip's size and CRC-32
        [[nodiscard]] Expected<void, std::string> Extract(const ZipEntry& entry, const Sink& sink);
        // Refused when the entry is larger than limit
        [[nodiscard]] Expected<std::string, std::string> ReadText(const ZipEntry& entry, std::size_t limit = 1024 * 1024);

    private:
        std::ifstream m_File;
        std::uint64_t m_DirectoryOffset = 0;
        std::vector<ZipEntry> m_Entries;
    };
}