#include "Trinity/FileSystem/ZipWriter.hpp"

#include "Trinity/Core/Hash.hpp"
#include "Trinity/FileSystem/FileSystemUtilities.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <span>
#include <system_error>
#include <utility>

namespace Trinity
{
    namespace
    {
        using FileSystemUtilities::ToUtf8;

        constexpr std::uint32_t c_LocalSignature = 0x04034B50u;
        constexpr std::uint32_t c_CentralSignature = 0x02014B50u;
        constexpr std::uint32_t c_EndSignature = 0x06054B50u;
        constexpr std::uint32_t c_Zip64EndSignature = 0x06064B50u;
        constexpr std::uint32_t c_Zip64LocatorSignature = 0x07064B50u;
        constexpr std::uint16_t c_Zip64Extra = 0x0001u;
        // Info-ZIP's extended timestamp, which holds the time in UTC as Unix does, where DOS's has no zone
        constexpr std::uint16_t c_TimeExtra = 0x5455u;
        // Names are UTF-8
        constexpr std::uint16_t c_Utf8Flag = 0x0800u;
        constexpr std::uint16_t c_Version = 20;
        constexpr std::uint16_t c_Zip64Version = 45;
        // Made on Unix, so tools read the permissions from the upper half of an entry's external attributes, wherever the zip was made
        constexpr std::uint16_t c_MadeBy = (3u << 8) | c_Zip64Version;
        constexpr std::uint32_t c_DirectoryAttribute = 0x10u;
        constexpr std::uint32_t c_FileType = 0100000u;
        constexpr std::uint32_t c_DirectoryType = 0040000u;
        constexpr std::uint64_t c_Max32 = 0xFFFFFFFFu;
        constexpr std::uint64_t c_Max16 = 0xFFFFu;
        // Where the CRC-32 is in a local header, written there again once the file is read
        constexpr std::uint64_t c_LocalCrcOffset = 14;
        constexpr std::size_t c_PieceSize = std::size_t{ 1 } << 20;

        // Little-endian, as a zip holds every number
        class Bytes
        {
        public:
            void Put8(std::uint64_t value) { Put(value, 1); }
            void Put16(std::uint64_t value) { Put(value, 2); }
            void Put32(std::uint64_t value) { Put(value, 4); }
            void Put64(std::uint64_t value) { Put(value, 8); }
            void PutText(std::string_view text)
            {
                for (const char it_Character : text)
                {
                    m_Data.push_back(static_cast<std::byte>(it_Character));
                }
            }

            [[nodiscard]] const std::vector<std::byte>& GetData() const { return m_Data; }

        private:
            void Put(std::uint64_t value, std::size_t size)
            {
                for (std::size_t it_Byte = 0; it_Byte < size; ++it_Byte)
                {
                    m_Data.push_back(static_cast<std::byte>((value >> (it_Byte * 8)) & 0xFFu));
                }
            }

            std::vector<std::byte> m_Data;
        };

        // DOS's time, to two seconds, from 1980 to 2107, and Unix's, each read as UTC
        void SetTime(std::filesystem::file_time_type time, std::uint16_t& dosTime, std::uint16_t& dosDate, std::uint32_t& unixTime)
        {
            const std::chrono::sys_seconds l_Seconds = std::chrono::floor<std::chrono::seconds>(std::chrono::clock_cast<std::chrono::system_clock>(time));
            const std::chrono::sys_days l_Days = std::chrono::floor<std::chrono::days>(l_Seconds);
            const std::chrono::year_month_day l_Date{ l_Days };
            const std::chrono::hh_mm_ss<std::chrono::seconds> l_Time{ l_Seconds - l_Days };
            const int l_Year = static_cast<int>(l_Date.year());
            unixTime = static_cast<std::uint32_t>(std::clamp<std::int64_t>(l_Seconds.time_since_epoch().count(), 0, static_cast<std::int64_t>(c_Max32)));
            if (l_Year < 1980 || l_Year > 2107)
            {
                dosDate = static_cast<std::uint16_t>(l_Year < 1980 ? (1u << 5) | 1u : (127u << 9) | (12u << 5) | 31u);
                dosTime = static_cast<std::uint16_t>(l_Year < 1980 ? 0u : (23u << 11) | (59u << 5) | 29u);

                return;
            }

            dosDate = static_cast<std::uint16_t>((static_cast<unsigned>(l_Year - 1980) << 9) | (static_cast<unsigned>(l_Date.month()) << 5) | static_cast<unsigned>(l_Date.day()));
            dosTime = static_cast<std::uint16_t>((static_cast<unsigned>(l_Time.hours().count()) << 11) | (static_cast<unsigned>(l_Time.minutes().count()) << 5) | (static_cast<unsigned>(l_Time.seconds().count()) / 2));
        }

        std::uint32_t GetPermissions(const std::filesystem::path& path)
        {
            std::error_code l_Error;
            const std::filesystem::perms l_Permissions = std::filesystem::status(path, l_Error).permissions();

            return l_Error || l_Permissions == std::filesystem::perms::unknown ? 0644u : static_cast<std::uint32_t>(l_Permissions & std::filesystem::perms::mask) & 0777u;
        }

        bool IsDirectory(const std::string& name)
        {
            return name.ends_with('/');
        }
    }

    Expected<ZipWriter, std::string> ZipWriter::Create(const std::filesystem::path& path)
    {
        ZipWriter l_Writer;
        l_Writer.m_Path = path;
        l_Writer.m_File.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!l_Writer.m_File)
        {
            return Unexpected{ std::format("{} could not be created", ToUtf8(path)) };
        }

        return l_Writer;
    }

    Expected<void, std::string> ZipWriter::AddDirectory(std::string_view name, const std::filesystem::path& folder)
    {
        Entry l_Entry;
        l_Entry.Name = std::string(name);
        if (!IsDirectory(l_Entry.Name))
        {
            l_Entry.Name += '/';
        }

        std::error_code l_Error;
        const std::filesystem::file_time_type l_Time = std::filesystem::last_write_time(folder, l_Error);
        SetTime(l_Error ? std::filesystem::file_time_type::clock::now() : l_Time, l_Entry.DosTime, l_Entry.DosDate, l_Entry.UnixTime);
        l_Entry.Mode = c_DirectoryType | GetPermissions(folder);
        l_Entry.Offset = m_Offset;
        if (Expected<void, std::string> l_Written = WriteLocalHeader(l_Entry); !l_Written)
        {
            return l_Written;
        }

        m_Entries.push_back(std::move(l_Entry));

        return {};
    }

    // Read once, its bytes written as they are read, and its CRC-32 written back into its header once known, so the file is read but once
    Expected<void, std::string> ZipWriter::AddFile(std::string_view name, const std::filesystem::path& file)
    {
        std::error_code l_Error;
        const std::uintmax_t l_Size = std::filesystem::file_size(file, l_Error);
        const std::filesystem::file_time_type l_Time = l_Error ? std::filesystem::file_time_type() : std::filesystem::last_write_time(file, l_Error);
        std::ifstream l_Input(file, std::ios::binary);
        if (l_Error || !l_Input)
        {
            return Unexpected{ std::format("{} could not be read{}", ToUtf8(file), l_Error ? std::format(": {}", l_Error.message()) : std::string()) };
        }

        Entry l_Entry;
        l_Entry.Name = std::string(name);
        l_Entry.Size = l_Size;
        l_Entry.Offset = m_Offset;
        l_Entry.Mode = c_FileType | GetPermissions(file);
        SetTime(l_Time, l_Entry.DosTime, l_Entry.DosDate, l_Entry.UnixTime);
        if (Expected<void, std::string> l_Written = WriteLocalHeader(l_Entry); !l_Written)
        {
            return l_Written;
        }

        std::vector<std::byte> l_Piece(c_PieceSize);
        std::uint64_t l_Read = 0;
        while (l_Input)
        {
            l_Input.read(reinterpret_cast<char*>(l_Piece.data()), static_cast<std::streamsize>(l_Piece.size()));
            const std::size_t l_Count = static_cast<std::size_t>(l_Input.gcount());
            if (l_Count == 0)
            {
                break;
            }

            l_Entry.Crc = Hash::Crc32(std::span<const std::byte>(l_Piece.data(), l_Count), l_Entry.Crc);
            m_File.write(reinterpret_cast<const char*>(l_Piece.data()), static_cast<std::streamsize>(l_Count));
            l_Read += l_Count;
        }

        if (l_Input.bad() || l_Read != l_Size)
        {
            return Unexpected{ std::format("{} was {} bytes as it was read, not {}, as it changed or could not all be read", ToUtf8(file), l_Read, l_Size) };
        }

        const std::streampos l_End = m_File.tellp();
        m_File.seekp(static_cast<std::streamoff>(l_Entry.Offset + c_LocalCrcOffset));
        Bytes l_Crc;
        l_Crc.Put32(l_Entry.Crc);
        m_File.write(reinterpret_cast<const char*>(l_Crc.GetData().data()), static_cast<std::streamsize>(l_Crc.GetData().size()));
        m_File.seekp(l_End);
        if (!m_File)
        {
            return Unexpected{ std::format("{} could not be written", ToUtf8(m_Path)) };
        }

        m_Offset += l_Size;
        m_Entries.push_back(std::move(l_Entry));

        return {};
    }

    // A size of 4 GB or more is given in a Zip64 field, its 32-bit fields holding all ones
    Expected<void, std::string> ZipWriter::WriteLocalHeader(const Entry& entry)
    {
        if (entry.Name.empty() || entry.Name.size() > c_Max16)
        {
            return Unexpected{ std::format("an entry of {} has a name {} bytes long, where a zip takes from 1 to {}", ToUtf8(m_Path), entry.Name.size(), c_Max16) };
        }

        const bool l_Zip64 = entry.Size >= c_Max32;
        Bytes l_Header;
        l_Header.Put32(c_LocalSignature);
        l_Header.Put16(l_Zip64 ? c_Zip64Version : c_Version);
        l_Header.Put16(c_Utf8Flag);
        l_Header.Put16(0);
        l_Header.Put16(entry.DosTime);
        l_Header.Put16(entry.DosDate);
        l_Header.Put32(entry.Crc);
        l_Header.Put32(l_Zip64 ? c_Max32 : entry.Size);
        l_Header.Put32(l_Zip64 ? c_Max32 : entry.Size);
        l_Header.Put16(entry.Name.size());
        l_Header.Put16((l_Zip64 ? 20u : 0u) + 9u);
        l_Header.PutText(entry.Name);
        if (l_Zip64)
        {
            l_Header.Put16(c_Zip64Extra);
            l_Header.Put16(16);
            l_Header.Put64(entry.Size);
            l_Header.Put64(entry.Size);
        }

        // A flag saying the modification time follows, then the time
        l_Header.Put16(c_TimeExtra);
        l_Header.Put16(5);
        l_Header.Put8(1);
        l_Header.Put32(entry.UnixTime);

        const std::vector<std::byte>& l_Data = l_Header.GetData();
        m_File.write(reinterpret_cast<const char*>(l_Data.data()), static_cast<std::streamsize>(l_Data.size()));
        if (!m_File)
        {
            return Unexpected{ std::format("{} could not be written", ToUtf8(m_Path)) };
        }

        m_Offset += l_Data.size();

        return {};
    }

    // Zip64's end records go before the classic one once the zip holds more than 65535 entries or its directory starts or ends past 4 GB
    Expected<void, std::string> ZipWriter::Finish()
    {
        const std::uint64_t l_DirectoryOffset = m_Offset;
        Bytes l_Directory;
        for (const Entry& it_Entry : m_Entries)
        {
            const bool l_BigSize = it_Entry.Size >= c_Max32;
            const bool l_BigOffset = it_Entry.Offset >= c_Max32;
            const std::uint16_t l_Zip64Size = static_cast<std::uint16_t>((l_BigSize ? 16u : 0u) + (l_BigOffset ? 8u : 0u));
            l_Directory.Put32(c_CentralSignature);
            l_Directory.Put16(c_MadeBy);
            l_Directory.Put16(l_BigSize || l_BigOffset ? c_Zip64Version : c_Version);
            l_Directory.Put16(c_Utf8Flag);
            l_Directory.Put16(0);
            l_Directory.Put16(it_Entry.DosTime);
            l_Directory.Put16(it_Entry.DosDate);
            l_Directory.Put32(it_Entry.Crc);
            l_Directory.Put32(l_BigSize ? c_Max32 : it_Entry.Size);
            l_Directory.Put32(l_BigSize ? c_Max32 : it_Entry.Size);
            l_Directory.Put16(it_Entry.Name.size());
            l_Directory.Put16((l_Zip64Size != 0 ? 4u + l_Zip64Size : 0u) + 9u);
            l_Directory.Put16(0);
            l_Directory.Put16(0);
            l_Directory.Put16(0);
            l_Directory.Put32((it_Entry.Mode << 16) | (IsDirectory(it_Entry.Name) ? c_DirectoryAttribute : 0u));
            l_Directory.Put32(l_BigOffset ? c_Max32 : it_Entry.Offset);
            l_Directory.PutText(it_Entry.Name);
            if (l_Zip64Size != 0)
            {
                l_Directory.Put16(c_Zip64Extra);
                l_Directory.Put16(l_Zip64Size);
                if (l_BigSize)
                {
                    l_Directory.Put64(it_Entry.Size);
                    l_Directory.Put64(it_Entry.Size);
                }

                if (l_BigOffset)
                {
                    l_Directory.Put64(it_Entry.Offset);
                }
            }

            l_Directory.Put16(c_TimeExtra);
            l_Directory.Put16(5);
            l_Directory.Put8(1);
            l_Directory.Put32(it_Entry.UnixTime);
        }

        const std::vector<std::byte>& l_Data = l_Directory.GetData();
        const std::uint64_t l_DirectorySize = l_Data.size();
        Bytes l_End;
        const bool l_Zip64 = m_Entries.size() > c_Max16 || l_DirectoryOffset >= c_Max32 || l_DirectoryOffset + l_DirectorySize >= c_Max32;
        if (l_Zip64)
        {
            const std::uint64_t l_Zip64EndOffset = l_DirectoryOffset + l_DirectorySize;
            l_End.Put32(c_Zip64EndSignature);
            l_End.Put64(44);
            l_End.Put16(c_MadeBy);
            l_End.Put16(c_Zip64Version);
            l_End.Put32(0);
            l_End.Put32(0);
            l_End.Put64(m_Entries.size());
            l_End.Put64(m_Entries.size());
            l_End.Put64(l_DirectorySize);
            l_End.Put64(l_DirectoryOffset);
            l_End.Put32(c_Zip64LocatorSignature);
            l_End.Put32(0);
            l_End.Put64(l_Zip64EndOffset);
            l_End.Put32(1);
        }

        l_End.Put32(c_EndSignature);
        l_End.Put16(0);
        l_End.Put16(0);
        l_End.Put16(std::min<std::uint64_t>(m_Entries.size(), c_Max16));
        l_End.Put16(std::min<std::uint64_t>(m_Entries.size(), c_Max16));
        l_End.Put32(std::min(l_DirectorySize, c_Max32));
        l_End.Put32(std::min(l_DirectoryOffset, c_Max32));
        l_End.Put16(0);

        m_File.write(reinterpret_cast<const char*>(l_Data.data()), static_cast<std::streamsize>(l_Data.size()));
        m_File.write(reinterpret_cast<const char*>(l_End.GetData().data()), static_cast<std::streamsize>(l_End.GetData().size()));
        m_File.close();
        if (m_File.fail())
        {
            return Unexpected{ std::format("{} could not be written", ToUtf8(m_Path)) };
        }

        m_Offset += l_Data.size() + l_End.GetData().size();

        return {};
    }
}