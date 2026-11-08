#include "Trinity/Core/Version.hpp"

#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Audio/AudioMixerData.hpp"
#include "Trinity/Audio/SoundEventData.hpp"
#include "Trinity/FileSystem/PackFormat.hpp"
#include "Trinity/Input/InputActionData.hpp"
#include "Trinity/Project/Project.hpp"
#include "Trinity/Scene/Prefab.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"

// The build writes it into its own folder for each configuration, from CMake/TrinityVersion.cmake and git
#include "TrinityBuildInfo.hpp"

#include <algorithm>
#include <format>
#include <vector>

namespace Trinity
{
    namespace
    {
        bool IsNumber(std::string_view text)
        {
            return !text.empty() && std::ranges::all_of(text, [](char character) { return character >= '0' && character <= '9'; });
        }

        // By value, however long, without reading them into an integer that could overflow
        std::strong_ordering CompareNumbers(std::string_view left, std::string_view right)
        {
            const auto l_Trim = [](std::string_view text)
            {
                const std::size_t l_First = text.find_first_not_of('0');

                return l_First == std::string_view::npos ? std::string_view() : text.substr(l_First);
            };

            left = l_Trim(left);
            right = l_Trim(right);
            if (left.size() != right.size())
            {
                return left.size() <=> right.size();
            }

            return left <=> right;
        }

        // A number before text, as Semantic Versioning orders identifiers
        std::strong_ordering CompareIdentifiers(std::string_view left, std::string_view right)
        {
            const bool l_LeftNumber = IsNumber(left);
            const bool l_RightNumber = IsNumber(right);
            if (l_LeftNumber && l_RightNumber)
            {
                return CompareNumbers(left, right);
            }

            if (l_LeftNumber != l_RightNumber)
            {
                return l_LeftNumber ? std::strong_ordering::less : std::strong_ordering::greater;
            }

            return left <=> right;
        }

        std::vector<std::string_view> Split(std::string_view text, char separator)
        {
            std::vector<std::string_view> l_Parts;
            while (true)
            {
                const std::size_t l_End = text.find(separator);
                l_Parts.push_back(text.substr(0, l_End));
                if (l_End == std::string_view::npos)
                {
                    return l_Parts;
                }

                text.remove_prefix(l_End + 1);
            }
        }
    }

    // Trinity.trversion names the formats CMake/TrinityVersion.cmake lists, which must be the ones this engine reads and writes
    static_assert(Project::c_FormatVersion == TR_FORMAT_PROJECT, "Raise TRINITY_FORMAT_PROJECT in CMake/TrinityVersion.cmake to the project format");
    static_assert(SceneSerializer::c_FormatVersion == TR_FORMAT_SCENE, "Raise TRINITY_FORMAT_SCENE in CMake/TrinityVersion.cmake to the scene format");
    static_assert(Prefab::c_FormatVersion == TR_FORMAT_PREFAB, "Raise TRINITY_FORMAT_PREFAB in CMake/TrinityVersion.cmake to the prefab format");
    static_assert(InputActionsData::c_FormatVersion == TR_FORMAT_INPUT_ACTIONS, "Raise TRINITY_FORMAT_INPUT_ACTIONS in CMake/TrinityVersion.cmake to the input actions format");
    static_assert(AudioMixerData::c_FormatVersion == TR_FORMAT_AUDIO_MIXER, "Raise TRINITY_FORMAT_AUDIO_MIXER in CMake/TrinityVersion.cmake to the mixer format");
    static_assert(SoundEventData::c_FormatVersion == TR_FORMAT_SOUND_EVENT, "Raise TRINITY_FORMAT_SOUND_EVENT in CMake/TrinityVersion.cmake to the sound event format");
    static_assert(AssetRegistry::c_MetaFormatVersion == TR_FORMAT_META, "Raise TRINITY_FORMAT_META in CMake/TrinityVersion.cmake to the .meta format");
    static_assert(PackHeader::c_Version == TR_FORMAT_PACK, "Raise TRINITY_FORMAT_PACK in CMake/TrinityVersion.cmake to the pack format");

    const char* GetVersionString()
    {
        return TR_BUILD_VERSION;
    }

    std::string_view GetBuildCommit()
    {
        return TR_BUILD_COMMIT;
    }

    bool IsBuildModified()
    {
        return TR_BUILD_MODIFIED != 0;
    }

    std::string_view GetBuildDate()
    {
        return TR_BUILD_DATE;
    }

    std::string_view GetBuildPlatform()
    {
        return TR_BUILD_PLATFORM;
    }

    std::string_view GetBuildArchitecture()
    {
        return TR_BUILD_ARCHITECTURE;
    }

    std::string GetBuildCommitDescription()
    {
        const std::string_view l_Commit = GetBuildCommit();
        if (l_Commit.empty())
        {
            return "no commit";
        }

        return std::format("{}{}", l_Commit.substr(0, c_ShortCommitLength), IsBuildModified() ? ", modified" : "");
    }

    std::string GetVersionDescription()
    {
        return std::format("{} ({})", GetVersionString(), GetBuildCommitDescription());
    }

    // Build metadata after a plus is no part of the order, and a missing part counts as 0, so 0.11 and 0.11.0 are the same version
    std::strong_ordering CompareVersions(std::string_view left, std::string_view right)
    {
        left = left.substr(0, left.find('+'));
        right = right.substr(0, right.find('+'));
        const std::size_t l_LeftHyphen = left.find('-');
        const std::size_t l_RightHyphen = right.find('-');

        const std::vector<std::string_view> l_LeftCore = Split(left.substr(0, l_LeftHyphen), '.');
        const std::vector<std::string_view> l_RightCore = Split(right.substr(0, l_RightHyphen), '.');
        for (std::size_t it_Part = 0; it_Part < std::max(l_LeftCore.size(), l_RightCore.size()); ++it_Part)
        {
            const std::string_view l_Left = it_Part < l_LeftCore.size() ? l_LeftCore[it_Part] : std::string_view("0");
            const std::string_view l_Right = it_Part < l_RightCore.size() ? l_RightCore[it_Part] : std::string_view("0");
            if (const std::strong_ordering l_Order = CompareIdentifiers(l_Left, l_Right); l_Order != 0)
            {
                return l_Order;
            }
        }

        const bool l_LeftRelease = l_LeftHyphen == std::string_view::npos;
        const bool l_RightRelease = l_RightHyphen == std::string_view::npos;
        if (l_LeftRelease || l_RightRelease)
        {
            return l_LeftRelease == l_RightRelease ? std::strong_ordering::equal : (l_LeftRelease ? std::strong_ordering::greater : std::strong_ordering::less);
        }

        const std::vector<std::string_view> l_LeftPre = Split(left.substr(l_LeftHyphen + 1), '.');
        const std::vector<std::string_view> l_RightPre = Split(right.substr(l_RightHyphen + 1), '.');
        for (std::size_t it_Part = 0; it_Part < std::min(l_LeftPre.size(), l_RightPre.size()); ++it_Part)
        {
            if (const std::strong_ordering l_Order = CompareIdentifiers(l_LeftPre[it_Part], l_RightPre[it_Part]); l_Order != 0)
            {
                return l_Order;
            }
        }

        return l_LeftPre.size() <=> l_RightPre.size();
    }
}