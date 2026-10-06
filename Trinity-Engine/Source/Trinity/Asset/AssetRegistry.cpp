#include "Trinity/Asset/AssetRegistry.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/FileSystem/FileSystemUtilities.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <set>
#include <system_error>
#include <vector>

namespace Trinity
{
    namespace
    {
        enum class MetaResult : std::uint8_t
        {
            Read,
            Unreadable,
            Newer
        };

        std::string GetExtension(std::string_view path)
        {
            const std::size_t l_Slash = path.find_last_of('/');
            const std::size_t l_Dot = path.find_last_of('.');
            if (l_Dot == std::string_view::npos || (l_Slash != std::string_view::npos && l_Dot < l_Slash))
            {
                return {};
            }

            std::string l_Extension(path.substr(l_Dot));
            std::ranges::transform(l_Extension, l_Extension.begin(), FileSystemUtilities::ToLowerAscii);

            return l_Extension;
        }

        MetaResult ReadMeta(const std::string& metaPath, AssetRecord& record)
        {
            const Expected<std::string, FileError> l_Text = FileSystem::ReadText(metaPath);
            if (!l_Text)
            {
                return MetaResult::Unreadable;
            }

            YAML::Node l_Root;
            try
            {
                l_Root = YAML::Load(*l_Text);
            }
            catch (const YAML::Exception&)
            {
                return MetaResult::Unreadable;
            }

            if (!l_Root.IsMap() || !l_Root["Format"] || !l_Root["Format"].IsScalar() || !l_Root["ID"] || !l_Root["ID"].IsScalar())
            {
                return MetaResult::Unreadable;
            }

            const std::string l_FormatText = l_Root["Format"].Scalar();
            std::uint32_t l_Format = 0;
            const std::from_chars_result l_Parsed = std::from_chars(l_FormatText.data(), l_FormatText.data() + l_FormatText.size(), l_Format);
            if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_FormatText.data() + l_FormatText.size() || l_Format == 0)
            {
                return MetaResult::Unreadable;
            }

            if (l_Format > AssetRegistry::c_MetaFormatVersion)
            {
                return MetaResult::Newer;
            }

            const std::optional<UUID> l_ID = UUID::Parse(l_Root["ID"].Scalar());
            if (!l_ID || !l_ID->IsValid())
            {
                return MetaResult::Unreadable;
            }

            record.ID = *l_ID;
            if (const YAML::Node l_Importer = l_Root["Importer"]; l_Importer && l_Importer.IsScalar())
            {
                record.Importer = l_Importer.Scalar();
            }

            // Settings are flat keys and values
            if (const YAML::Node l_Settings = l_Root["Settings"]; l_Settings && l_Settings.IsMap())
            {
                record.Settings.clear();
                for (const auto& it_Setting : l_Settings)
                {
                    if (it_Setting.first.IsScalar() && it_Setting.second.IsScalar())
                    {
                        record.Settings.push_back({ it_Setting.first.Scalar(), it_Setting.second.Scalar() });
                    }
                }
            }

            return MetaResult::Read;
        }

        bool WriteMeta(const std::string& metaPath, const AssetRecord& record)
        {
            YAML::Emitter l_Emitter;
            l_Emitter << YAML::BeginMap;
            l_Emitter << YAML::Key << "Format" << YAML::Value << AssetRegistry::c_MetaFormatVersion;
            l_Emitter << YAML::Key << "ID" << YAML::Value << record.ID.ToString();
            l_Emitter << YAML::Key << "Importer" << YAML::Value << record.Importer;
            l_Emitter << YAML::Key << "Settings" << YAML::Value << YAML::BeginMap;
            for (const AssetSetting& it_Setting : record.Settings)
            {
                l_Emitter << YAML::Key << it_Setting.Key << YAML::Value << it_Setting.Value;
            }

            l_Emitter << YAML::EndMap;
            l_Emitter << YAML::EndMap;

            const Expected<void, FileError> l_Written = FileSystem::WriteText(metaPath, std::string(l_Emitter.c_str()) + "\n");
            if (!l_Written)
            {
                TR_CORE_ERROR("Assets: {} could not be written: {}", metaPath, ToString(l_Written.GetError()));
            }

            return static_cast<bool>(l_Written);
        }
    }

    AssetRegistry::AssetRegistry(std::string_view root) : m_Root(root)
    {

    }

    // What a new .meta holds for an importer. An existing .meta keeps the settings it has, and an importer reads a missing one as its default
    void AssetRegistry::SetDefaultSettings(std::string_view importer, AssetSettings settings)
    {
        m_Defaults.insert_or_assign(std::string(importer), std::move(settings));
    }

    // Every file gets a .meta beside it holding its UUID. A file and its .meta moved together keep the UUID. Existing .meta files are never rewritten here
    AssetScanReport AssetRegistry::Scan()
    {
        TR_PROFILE_FUNCTION();

        std::vector<std::string> l_Files;
        std::set<std::string> l_Metas;
        std::vector<std::string> l_Directories{ m_Root };
        while (!l_Directories.empty())
        {
            const std::string l_Directory = std::move(l_Directories.back());
            l_Directories.pop_back();

            const Expected<std::vector<DirectoryEntry>, FileError> l_Entries = FileSystem::List(l_Directory);
            if (!l_Entries)
            {
                continue;
            }

            for (const DirectoryEntry& it_Entry : *l_Entries)
            {
                if (it_Entry.Name.starts_with('.'))
                {
                    continue;
                }

                std::string l_Path = std::format("{}/{}", l_Directory, it_Entry.Name);
                if (it_Entry.Type == FileType::Directory)
                {
                    l_Directories.push_back(std::move(l_Path));
                }
                else if (l_Path.ends_with(c_MetaExtension))
                {
                    l_Metas.insert(std::move(l_Path));
                }
                else
                {
                    l_Files.push_back(std::move(l_Path));
                }
            }
        }

        std::ranges::sort(l_Files);

        AssetScanReport l_Report;
        RecordMap l_Records;
        PathMap l_ByPath;
        for (const std::string& it_File : l_Files)
        {
            const std::string l_MetaPath = it_File + std::string(c_MetaExtension);
            const bool l_HadMeta = l_Metas.erase(l_MetaPath) != 0;

            AssetRecord l_Record;
            l_Record.Path = it_File;
            l_Record.Importer = GetDefaultImporter(it_File);
            if (const auto a_Defaults = m_Defaults.find(l_Record.Importer); a_Defaults != m_Defaults.end())
            {
                l_Record.Settings = a_Defaults->second;
            }

            const MetaResult l_Result = l_HadMeta ? ReadMeta(l_MetaPath, l_Record) : MetaResult::Unreadable;
            if (l_Result == MetaResult::Newer)
            {
                TR_CORE_ERROR("Assets: {} was written by a newer Trinity, so {} is left out and its .meta left as it is", l_MetaPath, it_File);
                ++l_Report.Refused;

                continue;
            }

            bool l_NeedsID = l_Result != MetaResult::Read;
            if (!l_NeedsID && l_Records.contains(l_Record.ID))
            {
                TR_CORE_WARN("Assets: {} has the same UUID {} as {}, as a copied file and .meta would, so it gets a new one", it_File, l_Record.ID, l_Records.at(l_Record.ID).Path);
                l_NeedsID = true;
            }

            if (l_NeedsID)
            {
                const bool l_WasKnown = m_ByPath.contains(it_File);
                do
                {
                    l_Record.ID = UUID::Generate();
                } while (l_Records.contains(l_Record.ID) || m_Records.contains(l_Record.ID));

                if (!WriteMeta(l_MetaPath, l_Record))
                {
                    continue;
                }

                if (l_HadMeta || l_WasKnown)
                {
                    if (!l_HadMeta)
                    {
                        TR_CORE_WARN("Assets: {} lost its .meta, so it now has the new UUID {}, and anything that named its old one no longer finds it", it_File, l_Record.ID);
                    }
                    else if (l_Result == MetaResult::Unreadable)
                    {
                        TR_CORE_WARN("Assets: the .meta of {} could not be read, so it now has the new UUID {}", it_File, l_Record.ID);
                    }

                    ++l_Report.Regenerated;
                }
                else
                {
                    ++l_Report.Created;
                }
            }
            else if (const auto a_Old = m_Records.find(l_Record.ID); a_Old != m_Records.end() && a_Old->second.Path != it_File)
            {
                ++l_Report.Moved;
            }

            l_ByPath.emplace(it_File, l_Record.ID);
            l_Records.emplace(l_Record.ID, std::move(l_Record));
        }

        for (const auto& [it_ID, it_Record] : m_Records)
        {
            if (!l_Records.contains(it_ID))
            {
                ++l_Report.Removed;
            }
        }

        l_Report.Assets = l_Records.size();
        l_Report.Orphaned = l_Metas.size();
        m_Records = std::move(l_Records);
        m_ByPath = std::move(l_ByPath);

        // Forge scans each time it regains focus, so an unchanged folder stays quiet
        if (!m_Scanned || l_Report.Created != 0 || l_Report.Regenerated != 0 || l_Report.Moved != 0 || l_Report.Removed != 0 || l_Report.Refused != 0)
        {
            TR_CORE_INFO("Assets: {} under {}, {} new, {} moved, {} removed, {} given a new UUID, {} .meta without a file", l_Report.Assets, m_Root, l_Report.Created, l_Report.Moved, l_Report.Removed, l_Report.Regenerated, l_Report.Orphaned);
        }

        m_Scanned = true;

        return l_Report;
    }

    const AssetRecord* AssetRegistry::Find(UUID id) const
    {
        const auto a_Found = m_Records.find(id);

        return a_Found != m_Records.end() ? &a_Found->second : nullptr;
    }

    // Ordered by path, so work over every asset runs and logs in the same order each time
    std::vector<const AssetRecord*> AssetRegistry::GetRecords() const
    {
        std::vector<const AssetRecord*> l_Records;
        l_Records.reserve(m_Records.size());
        for (const auto& [it_ID, it_Record] : m_Records)
        {
            l_Records.push_back(&it_Record);
        }

        std::ranges::sort(l_Records, {}, &AssetRecord::Path);

        return l_Records;
    }

    const AssetRecord* AssetRegistry::FindByPath(std::string_view path) const
    {
        const auto a_Found = m_ByPath.find(std::string(path));

        return a_Found != m_ByPath.end() ? Find(a_Found->second) : nullptr;
    }

    std::string_view AssetRegistry::GetDefaultImporter(std::string_view path)
    {
        constexpr std::array<std::string_view, 6> c_TextureExtensions{ ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".psd" };

        const std::string l_Extension = GetExtension(path);
        if (std::ranges::find(c_TextureExtensions, l_Extension) != c_TextureExtensions.end())
        {
            return "Texture";
        }

        if (l_Extension == ".trscene")
        {
            return "Scene";
        }

        return "Binary";
    }
}