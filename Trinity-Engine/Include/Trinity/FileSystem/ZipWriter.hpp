#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // A zip written to a file on the disk, its entries stored as they are, uncompressed, as Forge backs a project up before moving it to a newer version. Names are UTF-8 with / between folders, each entry keeps its file's time, in UTC, and its permissions, and Zip64 is written once an entry or the zip passes 4 GB or it holds more than 65535 entries, which every common zip tool reads. A zip not finished is not whole, so whoever writes one removes it when it fails
    class TRINITY_API ZipWriter
    {
    public:
        ZipWriter() = default;

        ZipWriter(ZipWriter&&) = default;
        ZipWriter& operator=(ZipWriter&&) = default;

        ZipWriter(const ZipWriter&) = delete;
        ZipWriter& operator=(const ZipWriter&) = delete;

        // A new file, written over one there already, or why it could not be made
        [[nodiscard]] static Expected<ZipWriter, std::string> Create(const std::filesystem::path& path);

        // A folder, with its time and permissions, so one that is empty is kept. A name without a / at its end is given one
        [[nodiscard]] Expected<void, std::string> AddDirectory(std::string_view name, const std::filesystem::path& folder);
        // A file's bytes, read in pieces, refused when the file cannot be read or is not the size it was as it started
        [[nodiscard]] Expected<void, std::string> AddFile(std::string_view name, const std::filesystem::path& file);
        // The central directory after the entries, which makes the zip whole, and the file closed
        [[nodiscard]] Expected<void, std::string> Finish();

        [[nodiscard]] std::size_t GetEntryCount() const { return m_Entries.size(); }
        // Everything written so far, in bytes
        [[nodiscard]] std::uint64_t GetSize() const { return m_Offset; }

    private:
        struct Entry
        {
            std::string Name;
            std::uint64_t Size = 0;
            std::uint64_t Offset = 0;
            std::uint32_t Crc = 0;
            std::uint16_t DosTime = 0;
            std::uint16_t DosDate = 0;
            std::uint32_t UnixTime = 0;
            std::uint32_t Mode = 0;
        };

        // The entry's local header, with its CRC-32 as given, which AddFile writes again once it is known
        [[nodiscard]] Expected<void, std::string> WriteLocalHeader(const Entry& entry);

        std::ofstream m_File;
        std::filesystem::path m_Path;
        std::uint64_t m_Offset = 0;
        std::vector<Entry> m_Entries;
    };
}