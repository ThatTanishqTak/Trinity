#include "ProjectMigration.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <format>
#include <functional>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.generic_u8string();

        return std::string(l_Text.begin(), l_Text.end());
    }

    // The Format a file of Trinity's begins with, or none for text that names none
    std::optional<std::uint32_t> ReadFormat(std::string_view text)
    {
        constexpr std::string_view c_Key = "Format: ";
        const std::size_t l_At = text.starts_with(c_Key) ? 0 : text.find(std::string("\n").append(c_Key));
        if (l_At == std::string_view::npos)
        {
            return std::nullopt;
        }

        const std::size_t l_Start = l_At + (l_At == 0 ? 0 : 1) + c_Key.size();
        std::uint32_t l_Format = 0;
        const std::from_chars_result l_Parsed = std::from_chars(text.data() + l_Start, text.data() + text.size(), l_Format);

        return l_Parsed.ec == std::errc() ? std::optional<std::uint32_t>(l_Format) : std::nullopt;
    }

    // The text the file reads into, written as this version writes it, or why it cannot be read
    using Rewrite = std::function<Trinity::Expected<std::string, std::string>(const Trinity::AssetRecord& record, std::string_view text)>;

    Trinity::Expected<std::string, std::string> RewriteScene(const Trinity::AssetRecord&, std::string_view text)
    {
        Trinity::Scene l_Scene;
        if (Trinity::Expected<void, std::string> l_Loaded = Trinity::SceneSerializer::LoadFromText(l_Scene, text); !l_Loaded)
        {
            return Trinity::Unexpected{ l_Loaded.GetError() };
        }

        return Trinity::SceneSerializer::SaveToText(l_Scene);
    }

    Trinity::Expected<std::string, std::string> RewritePrefab(const Trinity::AssetRecord& record, std::string_view text)
    {
        Trinity::Scene l_Prefab;
        if (Trinity::Expected<void, std::string> l_Loaded = Trinity::SceneSerializer::LoadPrefabFromText(l_Prefab, text, record.ID); !l_Loaded)
        {
            return Trinity::Unexpected{ l_Loaded.GetError() };
        }

        return Trinity::SceneSerializer::SavePrefabTemplateToText(l_Prefab);
    }

    Trinity::Expected<std::string, std::string> RewriteInputActions(const Trinity::AssetRecord&, std::string_view text)
    {
        const Trinity::Expected<Trinity::InputActionsData, std::string> l_Data = Trinity::ParseInputActions(text);
        if (!l_Data)
        {
            return Trinity::Unexpected{ l_Data.GetError() };
        }

        return Trinity::WriteInputActions(*l_Data);
    }

    Trinity::Expected<std::string, std::string> RewriteAudioMixer(const Trinity::AssetRecord&, std::string_view text)
    {
        const Trinity::Expected<Trinity::AudioMixerData, std::string> l_Data = Trinity::ParseAudioMixer(text);
        if (!l_Data)
        {
            return Trinity::Unexpected{ l_Data.GetError() };
        }

        return Trinity::WriteAudioMixer(*l_Data);
    }

    Trinity::Expected<std::string, std::string> RewriteSoundEvent(const Trinity::AssetRecord&, std::string_view text)
    {
        const Trinity::Expected<Trinity::SoundEventData, std::string> l_Data = Trinity::ParseSoundEvent(text);
        if (!l_Data)
        {
            return Trinity::Unexpected{ l_Data.GetError() };
        }

        return Trinity::WriteSoundEvent(*l_Data);
    }

    // Each kind of file this version writes, by the importer the registry gives it, and its format now
    struct Kind
    {
        std::string_view Importer;
        std::uint32_t Format = 0;
        Rewrite Write;
    };

    std::vector<Kind> GetKinds()
    {
        return {
            { "Scene", Trinity::SceneSerializer::c_FormatVersion, RewriteScene },
            { Trinity::PrefabAsset::c_AssetType, Trinity::Prefab::c_FormatVersion, RewritePrefab },
            { Trinity::InputActionsAsset::c_AssetType, Trinity::InputActionsData::c_FormatVersion, RewriteInputActions },
            { Trinity::AudioMixerAsset::c_AssetType, Trinity::AudioMixerData::c_FormatVersion, RewriteAudioMixer },
            { Trinity::SoundEventAsset::c_AssetType, Trinity::SoundEventData::c_FormatVersion, RewriteSoundEvent }
        };
    }

    std::string DescribeFormats(std::optional<std::uint32_t> before, std::uint32_t after)
    {
        if (!before)
        {
            return std::format("in format {}", after);
        }

        return *before == after ? std::format("in format {}, as this version writes it", after) : std::format("from format {} to {}", *before, after);
    }
}

namespace ProjectMigration
{
    std::filesystem::path GetBackupPath(const std::filesystem::path& directory, std::string_view version)
    {
        const std::chrono::year_month_day l_Today{ std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()) };
        const std::string l_Stem = std::format("{}-{}-{:%Y-%m-%d}", ToUtf8(directory.filename()), version.empty() ? std::string_view("unknown") : version, std::chrono::sys_days(l_Today));
        std::filesystem::path l_Path = directory.parent_path() / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(l_Stem.data()), l_Stem.size()) + u8".zip");
        std::error_code l_Error;
        for (int it_Number = 2; std::filesystem::exists(l_Path, l_Error) && it_Number < 1000; ++it_Number)
        {
            const std::string l_Numbered = std::format("{} {}.zip", l_Stem, it_Number);
            l_Path = directory.parent_path() / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(l_Numbered.data()), l_Numbered.size()));
        }

        return l_Path;
    }

    // Entries in the order of their paths, so the same project backs up into the same zip
    Trinity::Expected<void, std::string> BackUp(const std::filesystem::path& directory, const std::filesystem::path& zip)
    {
        std::error_code l_Error;
        std::vector<std::filesystem::path> l_Paths;
        for (std::filesystem::recursive_directory_iterator it_Entry(directory, l_Error), it_End; !l_Error && it_Entry != it_End; it_Entry.increment(l_Error))
        {
            const std::filesystem::path l_Relative = it_Entry->path().lexically_relative(directory);
            if (it_Entry.depth() == 0 && it_Entry->is_directory(l_Error) && std::ranges::find(c_LeftOut, ToUtf8(l_Relative)) != c_LeftOut.end())
            {
                it_Entry.disable_recursion_pending();

                continue;
            }

            l_Paths.push_back(l_Relative);
        }

        if (l_Error)
        {
            return Trinity::Unexpected{ std::format("{} could not all be read: {}", ToUtf8(directory), l_Error.message()) };
        }

        std::ranges::sort(l_Paths, {}, [](const std::filesystem::path& path) { return ToUtf8(path); });

        std::filesystem::path l_Partial = zip;
        l_Partial += u8".partial";
        Trinity::Expected<Trinity::ZipWriter, std::string> l_Writer = Trinity::ZipWriter::Create(l_Partial);
        if (!l_Writer)
        {
            return Trinity::Unexpected{ l_Writer.GetError() };
        }

        const std::string l_Root = ToUtf8(directory.filename());
        const auto a_Write = [&]() -> Trinity::Expected<void, std::string>
        {
            if (Trinity::Expected<void, std::string> l_Added = l_Writer->AddDirectory(l_Root, directory); !l_Added)
            {
                return l_Added;
            }

            for (const std::filesystem::path& it_Path : l_Paths)
            {
                const std::filesystem::path l_Full = directory / it_Path;
                const std::string l_Name = std::format("{}/{}", l_Root, ToUtf8(it_Path));
                std::error_code l_TypeError;
                Trinity::Expected<void, std::string> l_Added;
                if (std::filesystem::is_directory(l_Full, l_TypeError))
                {
                    l_Added = l_Writer->AddDirectory(l_Name, l_Full);
                }
                else if (std::filesystem::is_regular_file(l_Full, l_TypeError))
                {
                    l_Added = l_Writer->AddFile(l_Name, l_Full);
                }
                else
                {
                    TR_WARN("Forge: {} is neither a file nor a folder, so the backup leaves it out", ToUtf8(l_Full));
                }

                if (!l_Added)
                {
                    return l_Added;
                }
            }

            return l_Writer->Finish();
        };

        if (Trinity::Expected<void, std::string> l_Written = a_Write(); !l_Written)
        {
            *l_Writer = Trinity::ZipWriter();
            std::filesystem::remove(l_Partial, l_Error);

            return l_Written;
        }

        std::filesystem::rename(l_Partial, zip, l_Error);
        if (l_Error)
        {
            std::filesystem::remove(l_Partial, l_Error);

            return Trinity::Unexpected{ std::format("{} could not be named {}", ToUtf8(l_Partial), ToUtf8(zip)) };
        }

        return {};
    }

    // Read from the files through the project's mounts, and written back through them only where the text differs, so a file this version writes the same keeps its time and is not imported again
    Report SaveAgain(Trinity::AssetRegistry& registry)
    {
        Report l_Report;
        const std::vector<Kind> l_Kinds = GetKinds();
        std::vector<const Trinity::AssetRecord*> l_Records = registry.GetRecords();
        std::ranges::sort(l_Records, {}, &Trinity::AssetRecord::Path);
        for (const Trinity::AssetRecord* it_Record : l_Records)
        {
            if (it_Record->Parent.IsValid())
            {
                continue;
            }

            // The .meta first, as this version writes it, once it is of an older format
            const std::string l_MetaPath = it_Record->Path + std::string(Trinity::AssetRegistry::c_MetaExtension);
            if (const Trinity::Expected<std::string, Trinity::FileError> l_Meta = Trinity::FileSystem::ReadText(l_MetaPath))
            {
                const std::optional<std::uint32_t> l_Format = ReadFormat(*l_Meta);
                if (l_Format && *l_Format < Trinity::AssetRegistry::c_MetaFormatVersion)
                {
                    if (registry.SetSettings(it_Record->ID, it_Record->Settings))
                    {
                        TR_INFO("Forge: rewrote {} {}", l_MetaPath, DescribeFormats(l_Format, Trinity::AssetRegistry::c_MetaFormatVersion));
                        ++l_Report.Rewritten;
                    }
                    else
                    {
                        TR_ERROR("Forge: {} could not be rewritten, so it is left in format {}", l_MetaPath, *l_Format);
                        ++l_Report.Failed;
                    }
                }
            }

            const auto a_Kind = std::ranges::find(l_Kinds, std::string_view(it_Record->Importer), &Kind::Importer);
            if (a_Kind == l_Kinds.end())
            {
                continue;
            }

            const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(it_Record->Path);
            if (!l_Text)
            {
                TR_ERROR("Forge: {} could not be read, so it is left as it is: {}", it_Record->Path, Trinity::ToString(l_Text.GetError()));
                ++l_Report.Failed;

                continue;
            }

            const Trinity::Expected<std::string, std::string> l_Written = a_Kind->Write(*it_Record, *l_Text);
            if (!l_Written)
            {
                TR_ERROR("Forge: {} could not be read, so it is left as it is: {}", it_Record->Path, l_Written.GetError());
                ++l_Report.Failed;

                continue;
            }

            if (*l_Written == *l_Text)
            {
                ++l_Report.Unchanged;

                continue;
            }

            if (const Trinity::Expected<void, Trinity::FileError> l_Saved = Trinity::FileSystem::WriteText(it_Record->Path, *l_Written); !l_Saved)
            {
                TR_ERROR("Forge: {} could not be written, so it is left as it was: {}", it_Record->Path, Trinity::ToString(l_Saved.GetError()));
                ++l_Report.Failed;

                continue;
            }

            TR_INFO("Forge: rewrote {} {}", it_Record->Path, DescribeFormats(ReadFormat(*l_Text), a_Kind->Format));
            ++l_Report.Rewritten;
        }

        return l_Report;
    }
}