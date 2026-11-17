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
    // Entries are stored uncompressed, with Zip64 past 4 GB or 65535 entries. Remove it on failure
    class TRINITY_API ZipWriter
    {
    public:
        ZipWriter() = default;

        ZipWriter(ZipWriter&&) = default;
        ZipWriter& operator=(ZipWriter&&) = default;

        ZipWriter(const ZipWriter&) = delete;
        ZipWriter& operator=(const ZipWriter&) = delete;

        // Overwrites a file already there
        [[nodiscard]] static Expected<ZipWriter, std::string> Create(const std::filesystem::path& path);

        // Kept even when empty. A name without a / at its end is given one
        [[nodiscard]] Expected<void, std::string> AddDirectory(std::string_view name, const std::filesystem::path& folder);
        // Refused when the file cannot be read, or changed size while being read
        [[nodiscard]] Expected<void, std::string> AddFile(std::string_view name, const std::filesystem::path& file);
        // Writes the central directory, without which the zip is not whole
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

        // AddFile writes the CRC-32 again once it is known
        [[nodiscard]] Expected<void, std::string> WriteLocalHeader(const Entry& entry);

        std::ofstream m_File;
        std::filesystem::path m_Path;
        std::uint64_t m_Offset = 0;
        std::vector<Entry> m_Entries;
    };
}