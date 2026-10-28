#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Trinity
{
    struct AssetSetting
    {
        std::string Key;
        std::string Value;
    };

    using AssetSettings = std::vector<AssetSetting>;

    // Something an importer made from a file, such as a model's mesh, named by a key the importer keeps the same from one import to the next
    struct SubAsset
    {
        std::string Key;
        std::string Importer;
        UUID ID;

        [[nodiscard]] bool operator==(const SubAsset&) const = default;
    };

    // A file's record lists its sub-assets. A sub-asset's own record has its file's path, then '#' and its key, and its file's UUID as its parent
    struct AssetRecord
    {
        UUID ID;
        std::string Path;
        std::string Importer;
        AssetSettings Settings;
        UUID Parent;
        std::vector<SubAsset> SubAssets;
        // Where a cooked game reads the asset's own file in its pack, for an asset whose file it reads as the project has it, such as a scene or a sound event. Empty in a project, whose files are read where they lie, and for an asset a loader finds cooked by its UUID
        std::string CookedPath;

        // The file the asset is read from where its loader reads it by its path
        [[nodiscard]] const std::string& GetReadPath() const { return CookedPath.empty() ? Path : CookedPath; }

        [[nodiscard]] const std::string* FindSetting(std::string_view key) const
        {
            const auto a_Found = std::ranges::find(Settings, key, &AssetSetting::Key);

            return a_Found != Settings.end() ? &a_Found->Value : nullptr;
        }
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
        static constexpr std::uint32_t c_MetaFormatVersion = 2;
        static constexpr char c_SubAssetSeparator = '#';

        explicit AssetRegistry(std::string_view root);

        void SetDefaultSettings(std::string_view importer, AssetSettings settings);
        AssetScanReport Scan();
        // In place of a scan, for a cooked game, which has no Assets folder: the records of the asset table its export wrote, which never change. Refused, keeping the records there were, for a table that is not whole or is from a newer Trinity
        [[nodiscard]] Expected<void, std::string> LoadTable(std::span<const std::byte> data);
        // Whether the records are a cooked game's, from LoadTable
        [[nodiscard]] bool IsCooked() const { return m_Cooked; }
        bool SetSettings(UUID id, AssetSettings settings);
        bool SetSubAssets(UUID parent, std::vector<SubAsset> subAssets);

        [[nodiscard]] const AssetRecord* Find(UUID id) const;
        [[nodiscard]] const AssetRecord* FindByPath(std::string_view path) const;
        [[nodiscard]] std::vector<const AssetRecord*> GetRecords() const;
        [[nodiscard]] std::size_t GetCount() const { return m_Records.size(); }
        [[nodiscard]] const std::string& GetRoot() const { return m_Root; }

        [[nodiscard]] static std::string_view GetDefaultImporter(std::string_view path);

    private:
        using RecordMap = std::unordered_map<UUID, AssetRecord, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, AssetRecord>, MemoryTag::Engine>>;
        using PathMap = std::unordered_map<std::string, UUID, std::hash<std::string>, std::equal_to<std::string>, TaggedAllocator<std::pair<const std::string, UUID>, MemoryTag::Engine>>;

        using DefaultsMap = std::unordered_map<std::string, AssetSettings, std::hash<std::string>, std::equal_to<std::string>, TaggedAllocator<std::pair<const std::string, AssetSettings>, MemoryTag::Engine>>;

        std::string m_Root;
        RecordMap m_Records;
        PathMap m_ByPath;
        DefaultsMap m_Defaults;
        bool m_Scanned = false;
        bool m_Cooked = false;
    };
}