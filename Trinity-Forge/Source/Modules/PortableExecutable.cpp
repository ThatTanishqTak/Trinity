#include "Modules/PortableExecutable.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>

namespace
{
    constexpr std::size_t c_SectionSize = 40;
    constexpr std::uint32_t c_ResourceDirectory = 2;
    constexpr std::uint32_t c_SecurityDirectory = 4;
    constexpr std::uint32_t c_RelocationDirectory = 5;
    constexpr std::uint32_t c_DebugDirectory = 6;
    constexpr std::uint32_t c_InitializedData = 0x00000040;
    constexpr std::uint32_t c_ResourceCharacteristics = 0x40000040;
    constexpr std::uint32_t c_Subdirectory = 0x80000000;
    // More than any program holds, so a damaged table cannot make one read go on and on
    constexpr std::size_t c_MaximumResources = 65536;

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

    template<typename T>
    void WriteAt(std::vector<char>& image, std::size_t offset, T value)
    {
        std::memcpy(image.data() + offset, &value, sizeof(T));
    }

    std::uint32_t AlignUp(std::uint32_t value, std::uint32_t alignment)
    {
        return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
    }

    // Where an image's headers lie: the COFF header, the optional header, its data directories and the section table
    struct Headers
    {
        std::size_t File = 0;
        std::size_t Optional = 0;
        std::size_t Directories = 0;
        std::uint32_t DirectoryCount = 0;
        std::size_t Sections = 0;
        std::uint16_t SectionCount = 0;
    };

    struct Section
    {
        std::uint32_t VirtualSize = 0;
        std::uint32_t VirtualAddress = 0;
        std::uint32_t RawSize = 0;
        std::uint32_t RawOffset = 0;
        std::uint32_t Characteristics = 0;
    };

    struct Directory
    {
        std::uint32_t Address = 0;
        std::uint32_t Size = 0;
    };

    std::optional<Headers> ReadHeaders(const std::vector<char>& image)
    {
        const std::optional<std::uint32_t> l_Header = ReadAt<std::uint32_t>(image, 0x3C);
        if (image.size() < 2 || image[0] != 'M' || image[1] != 'Z' || !l_Header || ReadAt<std::uint32_t>(image, *l_Header) != 0x00004550u)
        {
            return std::nullopt;
        }

        Headers l_Headers;
        l_Headers.File = static_cast<std::size_t>(*l_Header) + 4;
        l_Headers.Optional = l_Headers.File + 20;
        const std::optional<std::uint16_t> l_SectionCount = ReadAt<std::uint16_t>(image, l_Headers.File + 2);
        const std::optional<std::uint16_t> l_OptionalSize = ReadAt<std::uint16_t>(image, l_Headers.File + 16);
        const std::optional<std::uint16_t> l_Magic = ReadAt<std::uint16_t>(image, l_Headers.Optional);
        if (!l_SectionCount || !l_OptionalSize || !l_Magic || (*l_Magic != 0x10B && *l_Magic != 0x20B))
        {
            return std::nullopt;
        }

        // PE32+ has wider fields ahead of the data directories than PE32
        l_Headers.Directories = l_Headers.Optional + (*l_Magic == 0x20B ? 112 : 96);
        const std::optional<std::uint32_t> l_DirectoryCount = ReadAt<std::uint32_t>(image, l_Headers.Directories - 4);
        l_Headers.Sections = l_Headers.Optional + *l_OptionalSize;
        l_Headers.SectionCount = *l_SectionCount;
        if (!l_DirectoryCount || l_Headers.Directories + std::min<std::size_t>(*l_DirectoryCount, 16) * 8 > l_Headers.Sections || l_Headers.Sections + l_Headers.SectionCount * c_SectionSize > image.size())
        {
            return std::nullopt;
        }

        l_Headers.DirectoryCount = std::min<std::uint32_t>(*l_DirectoryCount, 16);

        return l_Headers;
    }

    std::vector<Section> ReadSections(const std::vector<char>& image, const Headers& headers)
    {
        std::vector<Section> l_Sections(headers.SectionCount);
        for (std::size_t it_Section = 0; it_Section < l_Sections.size(); ++it_Section)
        {
            const std::size_t l_Header = headers.Sections + it_Section * c_SectionSize;
            Section& l_Section = l_Sections[it_Section];
            l_Section.VirtualSize = *ReadAt<std::uint32_t>(image, l_Header + 8);
            l_Section.VirtualAddress = *ReadAt<std::uint32_t>(image, l_Header + 12);
            l_Section.RawSize = *ReadAt<std::uint32_t>(image, l_Header + 16);
            l_Section.RawOffset = *ReadAt<std::uint32_t>(image, l_Header + 20);
            l_Section.Characteristics = *ReadAt<std::uint32_t>(image, l_Header + 36);
        }

        return l_Sections;
    }

    Directory ReadDirectory(const std::vector<char>& image, const Headers& headers, std::uint32_t index)
    {
        if (index >= headers.DirectoryCount)
        {
            return {};
        }

        return { *ReadAt<std::uint32_t>(image, headers.Directories + index * 8), *ReadAt<std::uint32_t>(image, headers.Directories + index * 8 + 4) };
    }

    // The section that holds an address the image has once loaded
    std::optional<std::size_t> FindSection(const std::vector<Section>& sections, std::uint32_t address)
    {
        for (std::size_t it_Section = 0; it_Section < sections.size(); ++it_Section)
        {
            const Section& l_Section = sections[it_Section];
            if (address >= l_Section.VirtualAddress && address - l_Section.VirtualAddress < std::max(l_Section.VirtualSize, l_Section.RawSize))
            {
                return it_Section;
            }
        }

        return std::nullopt;
    }

    // Where an address the image has once loaded is in its file, through the section that holds it
    std::optional<std::size_t> ToFileOffset(const std::vector<Section>& sections, std::uint32_t address)
    {
        const std::optional<std::size_t> l_Section = FindSection(sections, address);
        if (!l_Section)
        {
            return std::nullopt;
        }

        return static_cast<std::size_t>(address - sections[*l_Section].VirtualAddress) + sections[*l_Section].RawOffset;
    }

    // One entry of a resource directory: what it names, and where what it points to lies from the table's start, a directory below when Subdirectory
    struct TableEntry
    {
        PortableExecutable::ResourceID Name;
        std::uint32_t Offset = 0;
        bool Subdirectory = false;
    };

    std::optional<std::vector<TableEntry>> ReadTable(const std::vector<char>& image, std::size_t base, std::uint32_t offset)
    {
        const std::size_t l_Table = base + offset;
        const std::optional<std::uint16_t> l_Named = ReadAt<std::uint16_t>(image, l_Table + 12);
        const std::optional<std::uint16_t> l_Numbered = ReadAt<std::uint16_t>(image, l_Table + 14);
        if (!l_Named || !l_Numbered)
        {
            return std::nullopt;
        }

        std::vector<TableEntry> l_Entries;
        for (std::size_t it_Entry = 0; it_Entry < static_cast<std::size_t>(*l_Named) + *l_Numbered; ++it_Entry)
        {
            const std::optional<std::uint32_t> l_Name = ReadAt<std::uint32_t>(image, l_Table + 16 + it_Entry * 8);
            const std::optional<std::uint32_t> l_Offset = ReadAt<std::uint32_t>(image, l_Table + 20 + it_Entry * 8);
            if (!l_Name || !l_Offset)
            {
                return std::nullopt;
            }

            TableEntry l_Entry;
            l_Entry.Offset = *l_Offset & ~c_Subdirectory;
            l_Entry.Subdirectory = (*l_Offset & c_Subdirectory) != 0;
            if ((*l_Name & c_Subdirectory) != 0)
            {
                // A name is its length in UTF-16 units, then the units
                const std::size_t l_String = base + (*l_Name & ~c_Subdirectory);
                const std::optional<std::uint16_t> l_Length = ReadAt<std::uint16_t>(image, l_String);
                if (!l_Length || l_String + 2 + static_cast<std::size_t>(*l_Length) * 2 > image.size())
                {
                    return std::nullopt;
                }

                l_Entry.Name.Name.resize(*l_Length);
                std::memcpy(l_Entry.Name.Name.data(), image.data() + l_String + 2, static_cast<std::size_t>(*l_Length) * 2);
            }
            else
            {
                l_Entry.Name.Number = static_cast<std::uint16_t>(*l_Name & 0xFFFF);
            }

            l_Entries.push_back(std::move(l_Entry));
        }

        return l_Entries;
    }

    // A name before a number, as the table has its named entries first, names in the order of their units and numbers rising, which Windows searches them in
    bool IsBefore(const PortableExecutable::ResourceID& left, const PortableExecutable::ResourceID& right)
    {
        if (left.Name.empty() != right.Name.empty())
        {
            return !left.Name.empty();
        }

        return left.Name.empty() ? left.Number < right.Number : left.Name < right.Name;
    }

    // The table Windows finds resources through, three levels deep, by type, name and language, then each resource's data entry, then the names, then the data, each piece on 8 bytes. Every offset is from the table's start but the data's, which is an address once loaded, the table being loaded at the address given. No time is written, so the same resources always give the same bytes
    std::vector<char> WriteTable(std::vector<PortableExecutable::Resource> resources, std::uint32_t address)
    {
        std::ranges::stable_sort(resources, [](const PortableExecutable::Resource& left, const PortableExecutable::Resource& right)
        {
            if (!(left.Type == right.Type))
            {
                return IsBefore(left.Type, right.Type);
            }

            if (!(left.Name == right.Name))
            {
                return IsBefore(left.Name, right.Name);
            }

            return left.Language < right.Language;
        });

        struct NameGroup
        {
            std::size_t First = 0;
            std::size_t Count = 0;
        };

        struct TypeGroup
        {
            std::vector<NameGroup> Names;
        };

        std::vector<TypeGroup> l_Types;
        for (std::size_t it_Resource = 0; it_Resource < resources.size(); ++it_Resource)
        {
            const bool l_NewType = it_Resource == 0 || !(resources[it_Resource].Type == resources[it_Resource - 1].Type);
            if (l_NewType)
            {
                l_Types.emplace_back();
            }

            if (l_NewType || !(resources[it_Resource].Name == resources[it_Resource - 1].Name))
            {
                l_Types.back().Names.push_back({ it_Resource, 0 });
            }

            ++l_Types.back().Names.back().Count;
        }

        const auto a_TableSize = [](std::size_t entries) { return static_cast<std::uint32_t>(16 + entries * 8); };
        const auto a_Align = [](std::uint32_t value) { return AlignUp(value, 8); };

        // Where each piece goes
        std::uint32_t l_Cursor = a_TableSize(l_Types.size());
        std::vector<std::uint32_t> l_NameTables;
        for (const TypeGroup& it_Type : l_Types)
        {
            l_NameTables.push_back(l_Cursor);
            l_Cursor += a_TableSize(it_Type.Names.size());
        }

        std::vector<std::vector<std::uint32_t>> l_LanguageTables(l_Types.size());
        for (std::size_t it_Type = 0; it_Type < l_Types.size(); ++it_Type)
        {
            for (const NameGroup& it_Name : l_Types[it_Type].Names)
            {
                l_LanguageTables[it_Type].push_back(l_Cursor);
                l_Cursor += a_TableSize(it_Name.Count);
            }
        }

        const std::uint32_t l_DataEntries = l_Cursor;
        l_Cursor += static_cast<std::uint32_t>(resources.size() * 16);
        std::vector<std::uint32_t> l_TypeStrings(l_Types.size(), 0);
        std::vector<std::uint32_t> l_NameStrings(resources.size(), 0);
        const auto a_PlaceString = [&l_Cursor](const PortableExecutable::ResourceID& id, std::uint32_t& offset)
        {
            if (!id.Name.empty())
            {
                offset = l_Cursor;
                l_Cursor += static_cast<std::uint32_t>(2 + id.Name.size() * 2);
            }
        };

        for (std::size_t it_Type = 0; it_Type < l_Types.size(); ++it_Type)
        {
            a_PlaceString(resources[l_Types[it_Type].Names.front().First].Type, l_TypeStrings[it_Type]);
            for (const NameGroup& it_Name : l_Types[it_Type].Names)
            {
                a_PlaceString(resources[it_Name.First].Name, l_NameStrings[it_Name.First]);
            }
        }

        std::vector<std::uint32_t> l_Data(resources.size());
        l_Cursor = a_Align(l_Cursor);
        for (std::size_t it_Resource = 0; it_Resource < resources.size(); ++it_Resource)
        {
            l_Data[it_Resource] = l_Cursor;
            l_Cursor = a_Align(l_Cursor + static_cast<std::uint32_t>(resources[it_Resource].Data.size()));
        }

        // Then each piece, into zeros
        std::vector<char> l_Table(l_Cursor, '\0');
        const auto a_WriteHeader = [&l_Table](std::uint32_t offset, std::size_t named, std::size_t numbered)
        {
            WriteAt<std::uint16_t>(l_Table, offset + 12, static_cast<std::uint16_t>(named));
            WriteAt<std::uint16_t>(l_Table, offset + 14, static_cast<std::uint16_t>(numbered));
        };
        const auto a_WriteEntry = [&l_Table](std::uint32_t table, std::size_t index, const PortableExecutable::ResourceID& id, std::uint32_t string, std::uint32_t target)
        {
            WriteAt<std::uint32_t>(l_Table, table + 16 + static_cast<std::uint32_t>(index) * 8, id.Name.empty() ? id.Number : (string | c_Subdirectory));
            WriteAt<std::uint32_t>(l_Table, table + 20 + static_cast<std::uint32_t>(index) * 8, target);
        };
        const auto a_WriteString = [&l_Table](const PortableExecutable::ResourceID& id, std::uint32_t offset)
        {
            if (!id.Name.empty())
            {
                WriteAt<std::uint16_t>(l_Table, offset, static_cast<std::uint16_t>(id.Name.size()));
                std::memcpy(l_Table.data() + offset + 2, id.Name.data(), id.Name.size() * 2);
            }
        };

        const std::size_t l_NamedTypes = static_cast<std::size_t>(std::ranges::count_if(l_Types, [&resources](const TypeGroup& type) { return !resources[type.Names.front().First].Type.Name.empty(); }));
        a_WriteHeader(0, l_NamedTypes, l_Types.size() - l_NamedTypes);
        std::size_t l_Leaf = 0;
        for (std::size_t it_Type = 0; it_Type < l_Types.size(); ++it_Type)
        {
            const TypeGroup& l_Type = l_Types[it_Type];
            const PortableExecutable::ResourceID& l_TypeID = resources[l_Type.Names.front().First].Type;
            a_WriteEntry(0, it_Type, l_TypeID, l_TypeStrings[it_Type], l_NameTables[it_Type] | c_Subdirectory);
            a_WriteString(l_TypeID, l_TypeStrings[it_Type]);

            const std::size_t l_Named = static_cast<std::size_t>(std::ranges::count_if(l_Type.Names, [&resources](const NameGroup& name) { return !resources[name.First].Name.Name.empty(); }));
            a_WriteHeader(l_NameTables[it_Type], l_Named, l_Type.Names.size() - l_Named);
            for (std::size_t it_Name = 0; it_Name < l_Type.Names.size(); ++it_Name)
            {
                const NameGroup& l_Name = l_Type.Names[it_Name];
                const std::uint32_t l_Languages = l_LanguageTables[it_Type][it_Name];
                a_WriteEntry(l_NameTables[it_Type], it_Name, resources[l_Name.First].Name, l_NameStrings[l_Name.First], l_Languages | c_Subdirectory);
                a_WriteString(resources[l_Name.First].Name, l_NameStrings[l_Name.First]);
                a_WriteHeader(l_Languages, 0, l_Name.Count);
                for (std::size_t it_Language = 0; it_Language < l_Name.Count; ++it_Language, ++l_Leaf)
                {
                    const PortableExecutable::Resource& l_Resource = resources[l_Name.First + it_Language];
                    const std::uint32_t l_Entry = l_DataEntries + static_cast<std::uint32_t>(l_Leaf) * 16;
                    a_WriteEntry(l_Languages, it_Language, { l_Resource.Language,{} }, 0, l_Entry);
                    WriteAt<std::uint32_t>(l_Table, l_Entry, address + l_Data[l_Leaf]);
                    WriteAt<std::uint32_t>(l_Table, l_Entry + 4, static_cast<std::uint32_t>(l_Resource.Data.size()));
                    if (!l_Resource.Data.empty())
                    {
                        std::memcpy(l_Table.data() + l_Data[l_Leaf], l_Resource.Data.data(), l_Resource.Data.size());
                    }
                }
            }
        }

        return l_Table;
    }

    // As Windows' CheckSumMappedFile adds it: the file's 16-bit words, folded as they are added, with the checksum's own place as zeros, then the file's length
    std::uint32_t ComputeChecksum(const std::vector<char>& image, std::size_t checksum)
    {
        std::uint64_t l_Sum = 0;
        for (std::size_t it_Byte = 0; it_Byte < image.size(); it_Byte += 2)
        {
            const auto a_Byte = [&](std::size_t index) -> std::uint32_t
            {
                return index >= image.size() || (index >= checksum && index < checksum + 4) ? 0u : static_cast<std::uint8_t>(image[index]);
            };

            l_Sum += a_Byte(it_Byte) | (a_Byte(it_Byte + 1) << 8);
            l_Sum = (l_Sum & 0xFFFF) + (l_Sum >> 16);
        }

        l_Sum = (l_Sum & 0xFFFF) + (l_Sum >> 16);

        return static_cast<std::uint32_t>(l_Sum + image.size());
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

    bool IsImage(const std::vector<char>& image)
    {
        return ReadHeaders(image).has_value();
    }

    bool SetPdbName(std::vector<char>& image, std::string_view name)
    {
        constexpr std::uint32_t c_CodeView = 2;
        constexpr std::size_t c_DebugEntrySize = 28;
        constexpr std::size_t c_RecordHeaderSize = 24;

        const std::optional<Headers> l_Headers = ReadHeaders(image);
        if (!l_Headers)
        {
            return false;
        }

        const Directory l_Debug = ReadDirectory(image, *l_Headers, c_DebugDirectory);
        if (l_Debug.Address == 0)
        {
            return false;
        }

        const std::optional<std::size_t> l_Entries = ToFileOffset(ReadSections(image, *l_Headers), l_Debug.Address);
        if (!l_Entries)
        {
            return false;
        }

        for (std::size_t it_Entry = 0; it_Entry < l_Debug.Size / c_DebugEntrySize; ++it_Entry)
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

    std::optional<std::vector<Resource>> ReadResources(const std::vector<char>& image)
    {
        const std::optional<Headers> l_Headers = ReadHeaders(image);
        if (!l_Headers)
        {
            return std::nullopt;
        }

        const Directory l_Directory = ReadDirectory(image, *l_Headers, c_ResourceDirectory);
        std::vector<Resource> l_Resources;
        if (l_Directory.Address == 0)
        {
            return l_Resources;
        }

        const std::vector<Section> l_Sections = ReadSections(image, *l_Headers);
        const std::optional<std::size_t> l_Base = ToFileOffset(l_Sections, l_Directory.Address);
        const std::optional<std::vector<TableEntry>> l_Types = l_Base ? ReadTable(image, *l_Base, 0) : std::nullopt;
        if (!l_Types)
        {
            return std::nullopt;
        }

        for (const TableEntry& it_Type : *l_Types)
        {
            const std::optional<std::vector<TableEntry>> l_Names = it_Type.Subdirectory ? ReadTable(image, *l_Base, it_Type.Offset) : std::nullopt;
            if (!l_Names)
            {
                return std::nullopt;
            }

            for (const TableEntry& it_Name : *l_Names)
            {
                const std::optional<std::vector<TableEntry>> l_Languages = it_Name.Subdirectory ? ReadTable(image, *l_Base, it_Name.Offset) : std::nullopt;
                if (!l_Languages)
                {
                    return std::nullopt;
                }

                for (const TableEntry& it_Language : *l_Languages)
                {
                    const std::optional<std::uint32_t> l_Address = ReadAt<std::uint32_t>(image, *l_Base + it_Language.Offset);
                    const std::optional<std::uint32_t> l_Size = ReadAt<std::uint32_t>(image, *l_Base + it_Language.Offset + 4);
                    const std::optional<std::size_t> l_Data = l_Address ? ToFileOffset(l_Sections, *l_Address) : std::nullopt;
                    if (it_Language.Subdirectory || !it_Language.Name.Name.empty() || !l_Size || !l_Data || *l_Data > image.size() || image.size() - *l_Data < *l_Size || l_Resources.size() >= c_MaximumResources)
                    {
                        return std::nullopt;
                    }

                    Resource& l_Resource = l_Resources.emplace_back();
                    l_Resource.Type = it_Type.Name;
                    l_Resource.Name = it_Name.Name;
                    l_Resource.Language = it_Language.Name.Number;
                    l_Resource.Data.assign(image.begin() + static_cast<std::ptrdiff_t>(*l_Data), image.begin() + static_cast<std::ptrdiff_t>(*l_Data + *l_Size));
                }
            }
        }

        return l_Resources;
    }

    // The resource section and the relocations, which linkers put last, are written again after the sections before them, the new table first, so nothing else moves. An image without relocations has the table last, and one without resources gains a section for them
    Trinity::Expected<void, std::string> SetResources(std::vector<char>& image, std::span<const Resource> resources)
    {
        const std::optional<Headers> l_Headers = ReadHeaders(image);
        if (!l_Headers)
        {
            return Trinity::Unexpected{ std::string("it is not a PE image") };
        }

        const std::vector<Section> l_Sections = ReadSections(image, *l_Headers);
        if (*ReadAt<std::uint32_t>(image, l_Headers->File + 8) != 0)
        {
            return Trinity::Unexpected{ std::string("it carries COFF symbols, whose place in the file would move") };
        }

        if (ReadDirectory(image, *l_Headers, c_SecurityDirectory).Size != 0)
        {
            return Trinity::Unexpected{ std::string("it is signed, and no signature survives its resources changing. Sign it once it is exported") };
        }

        std::optional<std::vector<Resource>> l_Merged = ReadResources(image);
        if (!l_Merged)
        {
            return Trinity::Unexpected{ std::string("its resources cannot be read") };
        }

        std::erase_if(*l_Merged, [resources](const Resource& kept) { return std::ranges::any_of(resources, [&kept](const Resource& given) { return given.Type == kept.Type; }); });
        l_Merged->insert(l_Merged->end(), resources.begin(), resources.end());

        const Directory l_ResourceDirectory = ReadDirectory(image, *l_Headers, c_ResourceDirectory);
        const Directory l_RelocationDirectory = ReadDirectory(image, *l_Headers, c_RelocationDirectory);
        const std::optional<std::size_t> l_ResourceSection = l_ResourceDirectory.Address != 0 ? FindSection(l_Sections, l_ResourceDirectory.Address) : std::nullopt;
        const std::optional<std::size_t> l_RelocationSection = l_RelocationDirectory.Address != 0 ? FindSection(l_Sections, l_RelocationDirectory.Address) : std::nullopt;
        if ((l_ResourceDirectory.Address != 0 && !l_ResourceSection) || (l_RelocationDirectory.Address != 0 && !l_RelocationSection) || (l_ResourceSection && l_ResourceSection == l_RelocationSection))
        {
            return Trinity::Unexpected{ std::string("its resources or relocations lie outside a section of their own") };
        }

        // The two must be the last sections, in rising addresses, with nothing else pointing into them and nothing in the file past them
        const std::size_t l_Kept = l_Sections.size() - (l_ResourceSection ? 1 : 0) - (l_RelocationSection ? 1 : 0);
        const bool l_Rising = std::ranges::is_sorted(l_Sections, {}, &Section::VirtualAddress);
        if (!l_Rising || (l_ResourceSection && *l_ResourceSection < l_Kept) || (l_RelocationSection && *l_RelocationSection < l_Kept))
        {
            return Trinity::Unexpected{ std::string("its resources and relocations are not its last sections, as a linker leaves them") };
        }

        for (std::uint32_t it_Directory = 0; it_Directory < l_Headers->DirectoryCount; ++it_Directory)
        {
            const Directory l_Other = ReadDirectory(image, *l_Headers, it_Directory);
            const std::optional<std::size_t> l_Holder = l_Other.Address != 0 && it_Directory != c_ResourceDirectory && it_Directory != c_RelocationDirectory && it_Directory != c_SecurityDirectory ? FindSection(l_Sections, l_Other.Address) : std::nullopt;
            if (l_Holder && *l_Holder >= l_Kept)
            {
                return Trinity::Unexpected{ std::format("its data directory {} lies among its resources or relocations, which would move", it_Directory) };
            }
        }

        const std::uint32_t l_SectionAlignment = *ReadAt<std::uint32_t>(image, l_Headers->Optional + 32);
        const std::uint32_t l_FileAlignment = *ReadAt<std::uint32_t>(image, l_Headers->Optional + 36);
        const std::uint32_t l_HeadersSize = *ReadAt<std::uint32_t>(image, l_Headers->Optional + 60);
        std::size_t l_FileEnd = l_HeadersSize;
        std::size_t l_KeptFileEnd = l_HeadersSize;
        std::uint32_t l_KeptEnd = AlignUp(l_HeadersSize, l_SectionAlignment);
        std::size_t l_FirstData = image.size();
        for (std::size_t it_Section = 0; it_Section < l_Sections.size(); ++it_Section)
        {
            const Section& l_Section = l_Sections[it_Section];
            if (l_Section.RawSize != 0)
            {
                l_FileEnd = std::max<std::size_t>(l_FileEnd, static_cast<std::size_t>(l_Section.RawOffset) + l_Section.RawSize);
                l_FirstData = std::min<std::size_t>(l_FirstData, l_Section.RawOffset);
            }

            if (it_Section < l_Kept)
            {
                l_KeptFileEnd = l_Section.RawSize != 0 ? std::max<std::size_t>(l_KeptFileEnd, static_cast<std::size_t>(l_Section.RawOffset) + l_Section.RawSize) : l_KeptFileEnd;
                l_KeptEnd = std::max(l_KeptEnd, AlignUp(l_Section.VirtualAddress + std::max(l_Section.VirtualSize, l_Section.RawSize), l_SectionAlignment));
            }
        }

        if (image.size() != l_FileEnd)
        {
            return Trinity::Unexpected{ std::string(image.size() > l_FileEnd ? "it holds data past its sections, such as a signature or an installer's payload" : "it is cut short") };
        }

        const std::size_t l_SectionCount = l_Kept + 1 + (l_RelocationSection ? 1 : 0);
        if (l_Headers->Sections + l_SectionCount * c_SectionSize > std::min<std::size_t>(l_HeadersSize, l_FirstData))
        {
            return Trinity::Unexpected{ std::string("its headers have no room for another section") };
        }

        // The new image: what comes before the two as it was, then the table, then the relocations as they were
        const std::uint32_t l_ResourceAddress = l_KeptEnd;
        const std::vector<char> l_Table = WriteTable(std::move(*l_Merged), l_ResourceAddress);
        std::vector<char> l_Image(image.begin(), image.begin() + static_cast<std::ptrdiff_t>(l_KeptFileEnd));
        l_Image.resize(AlignUp(static_cast<std::uint32_t>(l_Image.size()), l_FileAlignment), '\0');
        const std::uint32_t l_ResourceOffset = static_cast<std::uint32_t>(l_Image.size());
        const std::uint32_t l_ResourceRawSize = AlignUp(static_cast<std::uint32_t>(l_Table.size()), l_FileAlignment);
        l_Image.insert(l_Image.end(), l_Table.begin(), l_Table.end());
        l_Image.resize(l_ResourceOffset + l_ResourceRawSize, '\0');

        std::array<char, c_SectionSize> l_ResourceHeader{};
        std::memcpy(l_ResourceHeader.data(), ".rsrc", 5);
        std::uint32_t l_ResourceCharacteristics = c_ResourceCharacteristics;
        std::int64_t l_InitializedChange = (l_ResourceCharacteristics & c_InitializedData) != 0 ? l_ResourceRawSize : 0;
        if (l_ResourceSection)
        {
            std::memcpy(l_ResourceHeader.data(), image.data() + l_Headers->Sections + *l_ResourceSection * c_SectionSize, 8);
            l_ResourceCharacteristics = l_Sections[*l_ResourceSection].Characteristics;
            l_InitializedChange = ((l_ResourceCharacteristics & c_InitializedData) != 0 ? static_cast<std::int64_t>(l_ResourceRawSize) - l_Sections[*l_ResourceSection].RawSize : 0);
        }

        std::array<char, c_SectionSize> l_RelocationHeader{};
        std::uint32_t l_RelocationAddress = 0;
        if (l_RelocationSection)
        {
            const Section& l_Relocations = l_Sections[*l_RelocationSection];
            std::memcpy(l_RelocationHeader.data(), image.data() + l_Headers->Sections + *l_RelocationSection * c_SectionSize, c_SectionSize);
            l_RelocationAddress = AlignUp(l_ResourceAddress + static_cast<std::uint32_t>(l_Table.size()), l_SectionAlignment);
            std::memcpy(l_RelocationHeader.data() + 12, &l_RelocationAddress, 4);
            const std::uint32_t l_RelocationOffset = static_cast<std::uint32_t>(l_Image.size());
            std::memcpy(l_RelocationHeader.data() + 20, &l_RelocationOffset, 4);
            l_Image.insert(l_Image.end(), image.begin() + l_Relocations.RawOffset, image.begin() + l_Relocations.RawOffset + l_Relocations.RawSize);
            l_Image.resize(l_RelocationOffset + AlignUp(l_Relocations.RawSize, l_FileAlignment), '\0');
        }

        // The section table from the first of the two, cleared past the last
        const std::size_t l_Table0 = l_Headers->Sections + l_Kept * c_SectionSize;
        std::fill(l_Image.begin() + static_cast<std::ptrdiff_t>(l_Table0), l_Image.begin() + static_cast<std::ptrdiff_t>(l_Headers->Sections + std::max<std::size_t>(l_Sections.size(), l_SectionCount) * c_SectionSize), '\0');
        const std::uint32_t l_TableSize = static_cast<std::uint32_t>(l_Table.size());
        std::memcpy(l_ResourceHeader.data() + 8, &l_TableSize, 4);
        std::memcpy(l_ResourceHeader.data() + 12, &l_ResourceAddress, 4);
        std::memcpy(l_ResourceHeader.data() + 16, &l_ResourceRawSize, 4);
        std::memcpy(l_ResourceHeader.data() + 20, &l_ResourceOffset, 4);
        std::memcpy(l_ResourceHeader.data() + 36, &l_ResourceCharacteristics, 4);
        std::memcpy(l_Image.data() + l_Table0, l_ResourceHeader.data(), c_SectionSize);
        std::uint32_t l_LastEnd = l_ResourceAddress + l_TableSize;
        if (l_RelocationSection)
        {
            std::memcpy(l_Image.data() + l_Table0 + c_SectionSize, l_RelocationHeader.data(), c_SectionSize);
            l_LastEnd = l_RelocationAddress + std::max(l_Sections[*l_RelocationSection].VirtualSize, l_Sections[*l_RelocationSection].RawSize);
            WriteAt<std::uint32_t>(l_Image, l_Headers->Directories + c_RelocationDirectory * 8, l_RelocationAddress + (l_RelocationDirectory.Address - l_Sections[*l_RelocationSection].VirtualAddress));
        }

        WriteAt<std::uint16_t>(l_Image, l_Headers->File + 2, static_cast<std::uint16_t>(l_SectionCount));
        WriteAt<std::uint32_t>(l_Image, l_Headers->Optional + 8, static_cast<std::uint32_t>(static_cast<std::int64_t>(*ReadAt<std::uint32_t>(image, l_Headers->Optional + 8)) + l_InitializedChange));
        WriteAt<std::uint32_t>(l_Image, l_Headers->Optional + 56, AlignUp(l_LastEnd, l_SectionAlignment));
        WriteAt<std::uint32_t>(l_Image, l_Headers->Directories + c_ResourceDirectory * 8, l_ResourceAddress);
        WriteAt<std::uint32_t>(l_Image, l_Headers->Directories + c_ResourceDirectory * 8 + 4, l_TableSize);
        WriteAt<std::uint32_t>(l_Image, l_Headers->Optional + 64, 0);
        WriteAt<std::uint32_t>(l_Image, l_Headers->Optional + 64, ComputeChecksum(l_Image, l_Headers->Optional + 64));
        image = std::move(l_Image);

        return {};
    }
}