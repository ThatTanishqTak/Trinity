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

#include <format>

namespace Trinity
{
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
}