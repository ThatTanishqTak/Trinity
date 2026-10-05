#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Trinity
{
    struct AssetRecord
    {
        UUID ID;
        std::string Path;
        std::string Importer;
        std::string Settings;
    };

    struct AssetScanReport
    {
        std::size_t Assets = 0;
        std::size_t Created = 0;
        std::size_t Regenerated = 0;
        std::size_t Moved = 0;
        std::size_t Removed = 0;
        std::size_t Orphaned = 0;
        std::size_t Refused = 0;
    };

    class TRINITY_API AssetRegistry
    {
    public:
        static constexpr std::string_view c_MetaExtension = ".meta";
        static constexpr std::uint32_t c_MetaFormatVersion = 1;

        explicit AssetRegistry(std::string_view root);

        AssetScanReport Scan();

        [[nodiscard]] const AssetRecord* Find(UUID id) const;
        [[nodiscard]] const AssetRecord* FindByPath(std::string_view path) const;
        [[nodiscard]] std::size_t GetCount() const { return m_Records.size(); }
        [[nodiscard]] const std::string& GetRoot() const { return m_Root; }

        [[nodiscard]] static std::string_view GetDefaultImporter(std::string_view path);

    private:
        using RecordMap = std::unordered_map<UUID, AssetRecord, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, AssetRecord>, MemoryTag::Engine>>;
        using PathMap = std::unordered_map<std::string, UUID, std::hash<std::string>, std::equal_to<std::string>, TaggedAllocator<std::pair<const std::string, UUID>, MemoryTag::Engine>>;

        std::string m_Root;
        RecordMap m_Records;
        PathMap m_ByPath;
        bool m_Scanned = false;
    };
}