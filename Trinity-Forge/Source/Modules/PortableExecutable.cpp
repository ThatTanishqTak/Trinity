#include "Modules/PortableExecutable.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

namespace
{
    template<typename T>
    std::optional<T> ReadAt(const std::vector<char>& image, std::size_t offset)
    {
        if (offset > image.size() || image.size() - offset < sizeof(T))
        {
            return std::nullopt;
        }

        T l_Value{};
        std::memcpy(&l_Value, image.data() + offset, sizeof(T));

        return l_Value;
    }

    // Where an address the image has once loaded is in its file, through the section that holds it
    std::optional<std::size_t> ToFileOffset(const std::vector<char>& image, std::size_t sections, std::uint16_t sectionCount, std::uint32_t address)
    {
        constexpr std::size_t c_SectionSize = 40;
        for (std::size_t it_Section = 0; it_Section < sectionCount; ++it_Section)
        {
            const std::size_t l_Header = sections + it_Section * c_SectionSize;
            const std::optional<std::uint32_t> l_VirtualSize = ReadAt<std::uint32_t>(image, l_Header + 8);
            const std::optional<std::uint32_t> l_VirtualAddress = ReadAt<std::uint32_t>(image, l_Header + 12);
            const std::optional<std::uint32_t> l_RawSize = ReadAt<std::uint32_t>(image, l_Header + 16);
            const std::optional<std::uint32_t> l_RawOffset = ReadAt<std::uint32_t>(image, l_Header + 20);
            if (!l_VirtualSize || !l_VirtualAddress || !l_RawSize || !l_RawOffset)
            {
                return std::nullopt;
            }

            if (address >= *l_VirtualAddress && address - *l_VirtualAddress < std::max(*l_VirtualSize, *l_RawSize))
            {
                return static_cast<std::size_t>(address - *l_VirtualAddress) + *l_RawOffset;
            }
        }

        return std::nullopt;
    }
}

namespace PortableExecutable
{
    std::optional<std::vector<char>> ReadImage(const std::filesystem::path& path)
    {
        std::ifstream l_File(path, std::ios::binary);
        if (!l_File)
        {
            return std::nullopt;
        }

        std::vector<char> l_Bytes{ std::istreambuf_iterator<char>(l_File), std::istreambuf_iterator<char>() };
        if (l_File.bad())
        {
            return std::nullopt;
        }

        return l_Bytes;
    }

    bool WriteImage(const std::filesystem::path& path, const std::vector<char>& bytes)
    {
        std::ofstream l_File(path, std::ios::binary | std::ios::trunc);
        l_File.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        l_File.close();

        return !l_File.fail();
    }

    bool SetPdbName(std::vector<char>& image, std::string_view name)
    {
        constexpr std::uint32_t c_DebugDirectory = 6;
        constexpr std::uint32_t c_CodeView = 2;
        constexpr std::size_t c_DebugEntrySize = 28;
        constexpr std::size_t c_RecordHeaderSize = 24;

        const std::optional<std::uint32_t> l_Header = ReadAt<std::uint32_t>(image, 0x3C);
        if (image.size() < 2 || image[0] != 'M' || image[1] != 'Z' || !l_Header || ReadAt<std::uint32_t>(image, *l_Header) != 0x00004550u)
        {
            return false;
        }

        const std::size_t l_File = static_cast<std::size_t>(*l_Header) + 4;
        const std::optional<std::uint16_t> l_SectionCount = ReadAt<std::uint16_t>(image, l_File + 2);
        const std::optional<std::uint16_t> l_OptionalSize = ReadAt<std::uint16_t>(image, l_File + 16);
        const std::size_t l_Optional = l_File + 20;
        const std::optional<std::uint16_t> l_Magic = ReadAt<std::uint16_t>(image, l_Optional);
        if (!l_SectionCount || !l_OptionalSize || !l_Magic || (*l_Magic != 0x10B && *l_Magic != 0x20B))
        {
            return false;
        }

        // PE32+ has wider fields ahead of the data directories than PE32
        const std::size_t l_Directories = l_Optional + (*l_Magic == 0x20B ? 112 : 96);
        const std::optional<std::uint32_t> l_DirectoryCount = ReadAt<std::uint32_t>(image, l_Directories - 4);
        const std::optional<std::uint32_t> l_DebugAddress = ReadAt<std::uint32_t>(image, l_Directories + c_DebugDirectory * 8);
        const std::optional<std::uint32_t> l_DebugSize = ReadAt<std::uint32_t>(image, l_Directories + c_DebugDirectory * 8 + 4);
        if (!l_DirectoryCount || *l_DirectoryCount <= c_DebugDirectory || !l_DebugAddress || !l_DebugSize || *l_DebugAddress == 0)
        {
            return false;
        }

        const std::size_t l_Sections = l_Optional + *l_OptionalSize;
        const std::optional<std::size_t> l_Entries = ToFileOffset(image, l_Sections, *l_SectionCount, *l_DebugAddress);
        if (!l_Entries)
        {
            return false;
        }

        for (std::size_t it_Entry = 0; it_Entry < *l_DebugSize / c_DebugEntrySize; ++it_Entry)
        {
            const std::size_t l_Entry = *l_Entries + it_Entry * c_DebugEntrySize;
            const std::optional<std::uint32_t> l_Type = ReadAt<std::uint32_t>(image, l_Entry + 12);
            const std::optional<std::uint32_t> l_DataSize = ReadAt<std::uint32_t>(image, l_Entry + 16);
            const std::optional<std::uint32_t> l_DataOffset = ReadAt<std::uint32_t>(image, l_Entry + 24);
            if (!l_Type || !l_DataSize || !l_DataOffset || *l_Type != c_CodeView || *l_DataSize <= c_RecordHeaderSize || *l_DataOffset > image.size() || image.size() - *l_DataOffset < *l_DataSize)
            {
                continue;
            }

            // RSDS, then the PDB's GUID and age, then its path ending in a zero
            char* l_Record = image.data() + *l_DataOffset;
            if (std::memcmp(l_Record, "RSDS", 4) != 0 || name.size() + 1 > *l_DataSize - c_RecordHeaderSize)
            {
                continue;
            }

            std::memset(l_Record + c_RecordHeaderSize, 0, *l_DataSize - c_RecordHeaderSize);
            std::memcpy(l_Record + c_RecordHeaderSize, name.data(), name.size());

            return true;
        }

        return false;
    }
}