#include "Trinity/Asset/AssetTable.hpp"

#include "Trinity/Project/Project.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <format>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Trinity
{
    namespace
    {
        static_assert(std::endian::native == std::endian::little, "Asset tables are written and read as little-endian, as every platform Trinity targets is.");

        // This header, a record for each asset, then their strings one after another
        struct TableHeader
        {
            static constexpr std::array<char, 8> c_Magic{ 'T', 'R', 'A', 'S', 'S', 'E', 'T', 'S' };

            std::array<char, 8> Magic = c_Magic;
            std::uint32_t Version = AssetTable::c_FormatVersion;
            std::uint32_t RecordCount = 0;
            std::uint32_t StringBytes = 0;
            std::uint32_t Padding = 0;
        };

        static_assert(sizeof(TableHeader) == 24);

        // A string's start among the strings and its length
        struct TableString
        {
            std::uint32_t Offset = 0;
            std::uint32_t Size = 0;
        };

        struct TableRecord
        {
            std::uint64_t ID = 0;
            std::uint64_t Parent = 0;
            TableString Path;
            TableString Importer;
            TableString CookedPath;
        };

        static_assert(sizeof(TableRecord) == 40);
    }

    namespace AssetTable
    {
        std::string GetPath()
        {
            return std::format("{}/{}", Project::c_CacheMount, c_FileName);
        }

        std::vector<std::byte> Write(std::span<const AssetRecord> records)
        {
            std::vector<const AssetRecord*> l_Sorted;
            l_Sorted.reserve(records.size());
            for (const AssetRecord& it_Record : records)
            {
                l_Sorted.push_back(&it_Record);
            }

            std::ranges::sort(l_Sorted, {}, [](const AssetRecord* record) { return record->ID.GetValue(); });

            // Each string once, an importer's especially, which most records share
            std::string l_Strings;
            std::unordered_map<std::string_view, TableString> l_Written;
            const auto a_Add = [&l_Strings, &l_Written](std::string_view text)
            {
                if (const auto a_Found = l_Written.find(text); a_Found != l_Written.end())
                {
                    return a_Found->second;
                }

                const TableString l_String{ static_cast<std::uint32_t>(l_Strings.size()), static_cast<std::uint32_t>(text.size()) };
                l_Strings += text;
                l_Written.emplace(text, l_String);

                return l_String;
            };

            std::vector<TableRecord> l_Records;
            l_Records.reserve(l_Sorted.size());
            for (const AssetRecord* it_Record : l_Sorted)
            {
                TableRecord l_Record;
                l_Record.ID = it_Record->ID.GetValue();
                l_Record.Parent = it_Record->Parent.GetValue();
                l_Record.Path = a_Add(it_Record->Path);
                l_Record.Importer = a_Add(it_Record->Importer);
                l_Record.CookedPath = a_Add(it_Record->CookedPath);
                l_Records.push_back(l_Record);
            }

            TableHeader l_Header;
            l_Header.RecordCount = static_cast<std::uint32_t>(l_Records.size());
            l_Header.StringBytes = static_cast<std::uint32_t>(l_Strings.size());

            const std::span<const std::byte> l_HeaderBytes = std::as_bytes(std::span(&l_Header, 1));
            const std::span<const std::byte> l_RecordBytes = std::as_bytes(std::span(l_Records));
            const std::span<const std::byte> l_StringBytes = std::as_bytes(std::span(l_Strings));

            std::vector<std::byte> l_Data;
            l_Data.reserve(l_HeaderBytes.size() + l_RecordBytes.size() + l_StringBytes.size());
            l_Data.insert(l_Data.end(), l_HeaderBytes.begin(), l_HeaderBytes.end());
            l_Data.insert(l_Data.end(), l_RecordBytes.begin(), l_RecordBytes.end());
            l_Data.insert(l_Data.end(), l_StringBytes.begin(), l_StringBytes.end());

            return l_Data;
        }

        Expected<std::vector<AssetRecord>, std::string> Read(std::span<const std::byte> data)
        {
            TableHeader l_Header;
            if (data.size() < sizeof(TableHeader))
            {
                return Unexpected{ std::string("too short to be an asset table") };
            }

            std::memcpy(&l_Header, data.data(), sizeof(TableHeader));
            if (l_Header.Magic != TableHeader::c_Magic || l_Header.Version == 0)
            {
                return Unexpected{ std::string("not an asset table") };
            }

            if (l_Header.Version > c_FormatVersion)
            {
                return Unexpected{ std::format("written in format {} by a newer Trinity, and this build reads format {} and older", l_Header.Version, c_FormatVersion) };
            }

            const std::uint64_t l_Size = sizeof(TableHeader) + std::uint64_t{ l_Header.RecordCount } * sizeof(TableRecord) + l_Header.StringBytes;
            if (l_Size != data.size())
            {
                return Unexpected{ std::format("{} bytes long where its header gives {}, so it is cut short or corrupted", data.size(), l_Size) };
            }

            const std::string_view l_Strings(reinterpret_cast<const char*>(data.data()) + data.size() - l_Header.StringBytes, l_Header.StringBytes);
            const auto a_String = [l_Strings](const TableString& string) -> std::optional<std::string_view>
            {
                if (string.Offset > l_Strings.size() || string.Size > l_Strings.size() - string.Offset)
                {
                    return std::nullopt;
                }

                return l_Strings.substr(string.Offset, string.Size);
            };

            std::vector<AssetRecord> l_Records;
            l_Records.reserve(l_Header.RecordCount);
            std::unordered_set<UUID> l_IDs;
            for (std::size_t it_Index = 0; it_Index < l_Header.RecordCount; ++it_Index)
            {
                TableRecord l_Record;
                std::memcpy(&l_Record, data.data() + sizeof(TableHeader) + it_Index * sizeof(TableRecord), sizeof(TableRecord));

                const std::optional<std::string_view> l_Path = a_String(l_Record.Path);
                const std::optional<std::string_view> l_Importer = a_String(l_Record.Importer);
                const std::optional<std::string_view> l_CookedPath = a_String(l_Record.CookedPath);
                if (l_Record.ID == 0 || !l_IDs.insert(UUID(l_Record.ID)).second || !l_Path || l_Path->empty() || !l_Importer || l_Importer->empty() || !l_CookedPath)
                {
                    return Unexpected{ std::format("its record {} is malformed", it_Index) };
                }

                AssetRecord& l_Asset = l_Records.emplace_back();
                l_Asset.ID = UUID(l_Record.ID);
                l_Asset.Parent = UUID(l_Record.Parent);
                l_Asset.Path = *l_Path;
                l_Asset.Importer = *l_Importer;
                l_Asset.CookedPath = *l_CookedPath;
            }

            return l_Records;
        }
    }
}