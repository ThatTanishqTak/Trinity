#include "Trinity/Project/Project.hpp"

#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/FileSystem/FileSystemUtilities.hpp"
#include "Trinity/Project/SettingsYaml.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        using FileSystemUtilities::FromUtf8;
        using FileSystemUtilities::ToUtf8;
        using SettingsYaml::ReadString;

        // The mount points are fixed, so one project is open at a time
        bool s_ProjectOpen = false;

        std::string GetProjectFilePath(const std::filesystem::path& filePath)
        {
            return std::format("{}/{}", Project::c_ProjectMount, ToUtf8(filePath.filename()));
        }
    }

    Project::Project(std::filesystem::path directory, std::filesystem::path filePath) : m_Directory(std::move(directory)), m_FilePath(std::move(filePath))
    {

    }

    Project::~Project()
    {
        if (m_Mounted)
        {
            FileSystem::Unmount(c_CacheMount);
            FileSystem::Unmount(c_AssetsMount);
            FileSystem::Unmount(c_ProjectMount);
            s_ProjectOpen = false;

            TR_CORE_INFO("Project: closed {}", m_Name);
        }
    }

    // The folder takes the project's name, and must be new or empty
    Expected<Scope<Project>, std::string> Project::Create(const std::filesystem::path& directory)
    {
        std::error_code l_Error;
        std::filesystem::path l_Directory = std::filesystem::absolute(directory, l_Error).lexically_normal();
        if (!l_Directory.has_filename())
        {
            l_Directory = l_Directory.parent_path();
        }

        const std::string l_Name = ToUtf8(l_Directory.filename());
        if (l_Name.empty())
        {
            return Unexpected{ std::format("{} cannot name a project", ToUtf8(directory)) };
        }

        if (std::filesystem::exists(l_Directory, l_Error) && !std::filesystem::is_empty(l_Directory, l_Error))
        {
            return Unexpected{ std::format("{} is not empty", ToUtf8(l_Directory)) };
        }

        std::filesystem::create_directories(l_Directory / "Assets" / "Scenes", l_Error);
        if (!l_Error)
        {
            std::filesystem::create_directories(l_Directory / "Cache", l_Error);
        }

        if (l_Error)
        {
            return Unexpected{ std::format("{} could not be created: {}", ToUtf8(l_Directory), l_Error.message()) };
        }

        Scope<Project> l_Project(new Project(l_Directory, l_Directory / FromUtf8(l_Name + std::string(c_Extension))));
        l_Project->m_Name = l_Name;
        l_Project->m_EngineVersion = GetVersionString();

        if (Expected<void, std::string> l_Mounted = l_Project->Mount(); !l_Mounted)
        {
            return Unexpected{ l_Mounted.GetError() };
        }

        if (Expected<void, std::string> l_Saved = l_Project->Save(); !l_Saved)
        {
            return Unexpected{ l_Saved.GetError() };
        }

        static_cast<void>(FileSystem::WriteText(std::format("{}/.gitignore", c_ProjectMount), "Cache/\nBuild/\n"));

        TR_CORE_INFO("Project: created {} in {}", l_Project->m_Name, ToUtf8(l_Directory));

        return l_Project;
    }

    // A project file, or a folder holding exactly one
    Expected<Scope<Project>, std::string> Project::Open(const std::filesystem::path& path)
    {
        std::error_code l_Error;
        std::filesystem::path l_File = std::filesystem::absolute(path, l_Error).lexically_normal();
        if (std::filesystem::is_directory(l_File, l_Error))
        {
            std::vector<std::filesystem::path> l_Found;
            for (const std::filesystem::directory_entry& it_Entry : std::filesystem::directory_iterator(l_File, l_Error))
            {
                if (it_Entry.is_regular_file(l_Error) && it_Entry.path().extension() == c_Extension)
                {
                    l_Found.push_back(it_Entry.path());
                }
            }

            if (l_Found.size() != 1)
            {
                return Unexpected{ std::format("{} holds {} {} files, not one", ToUtf8(l_File), l_Found.size(), c_Extension) };
            }

            l_File = l_Found.front();
        }

        if (l_File.extension() != c_Extension || !std::filesystem::is_regular_file(l_File, l_Error))
        {
            return Unexpected{ std::format("{} is not a {} file", ToUtf8(l_File), c_Extension) };
        }

        Scope<Project> l_Project(new Project(l_File.parent_path(), l_File));
        l_Project->m_Name = ToUtf8(l_File.stem());
        if (Expected<void, std::string> l_Mounted = l_Project->Mount(); !l_Mounted)
        {
            return Unexpected{ l_Mounted.GetError() };
        }

        const Expected<std::string, FileError> l_Text = FileSystem::ReadText(GetProjectFilePath(l_File));
        if (!l_Text)
        {
            return Unexpected{ std::format("{} could not be read: {}", ToUtf8(l_File), ToString(l_Text.GetError())) };
        }

        YAML::Node l_Root;
        try
        {
            l_Root = YAML::Load(*l_Text);
        }
        catch (const YAML::Exception& exception)
        {
            return Unexpected{ std::format("{} is not valid YAML: {}", ToUtf8(l_File), exception.what()) };
        }

        std::uint32_t l_Format = 0;
        const std::string l_FormatText = l_Root.IsMap() ? ReadString(l_Root, "Format", "") : std::string();
        const std::from_chars_result l_Parsed = std::from_chars(l_FormatText.data(), l_FormatText.data() + l_FormatText.size(), l_Format);
        if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_FormatText.data() + l_FormatText.size() || l_Format == 0)
        {
            return Unexpected{ std::format("{} has no valid Format version", ToUtf8(l_File)) };
        }

        if (l_Format > c_FormatVersion)
        {
            return Unexpected{ std::format("{} was written in format {} by a newer Trinity, and this build reads format {} and older", ToUtf8(l_File), l_Format, c_FormatVersion) };
        }

        l_Project->m_Name = ReadString(l_Root, "Name", ToUtf8(l_File.stem()));
        l_Project->m_EngineVersion = ReadString(l_Root, "EngineVersion", "");
        // Format 1 named the start scene by its path under Assets, which only the asset registry can turn into a UUID
        const std::string l_StartScene = ReadString(l_Root, "StartScene", "");
        if (l_Format == 1)
        {
            l_Project->m_StartScenePath = l_StartScene.empty() ? std::string() : std::format("{}/{}", c_AssetsMount, l_StartScene);
        }
        else if (!l_StartScene.empty())
        {
            const std::optional<UUID> l_Scene = UUID::Parse(l_StartScene);
            if (!l_Scene)
            {
                return Unexpected{ std::format("{} has a malformed StartScene", ToUtf8(l_File)) };
            }

            l_Project->m_StartScene = *l_Scene;
        }

        const std::string l_Owner = std::format("Project: {}", l_Project->m_Name);
        l_Project->m_Physics.Layers = l_Format >= 3 ? SettingsYaml::ReadCollisionLayers(l_Root["Physics"], l_Owner) : CollisionLayers();
        if (l_Format >= 4)
        {
            SettingsYaml::ReadWorldSettings(l_Root["Physics"], l_Owner, l_Project->m_Physics);
        }

        // A module or assembly name that cannot be one is reported and left out, so the project opens with none
        if (l_Format >= 5)
        {
            const YAML::Node l_Scripting = l_Root["Scripting"];
            const std::string l_Module = l_Scripting && l_Scripting.IsMap() ? ReadString(l_Scripting, "Module", "") : std::string();
            if (IsValidModuleName(l_Module))
            {
                l_Project->m_ModuleName = l_Module;
            }
            else if (!l_Module.empty())
            {
                TR_CORE_WARN("Project: {} names its C++ module '{}', which is not a name a module can have, so it opens with none", l_Project->m_Name, l_Module);
            }

            const std::string l_Assembly = l_Format >= 6 && l_Scripting && l_Scripting.IsMap() ? ReadString(l_Scripting, "Assembly", "") : std::string();
            if (IsValidModuleName(l_Assembly))
            {
                l_Project->m_AssemblyName = l_Assembly;
            }
            else if (!l_Assembly.empty())
            {
                TR_CORE_WARN("Project: {} names its C# assembly '{}', which is not a name an assembly can have here, so it opens with none", l_Project->m_Name, l_Assembly);
            }
        }

        // An asset that cannot be one is reported and left out, so the project opens with none
        if (l_Format >= 7)
        {
            const YAML::Node l_Input = l_Root["Input"];
            const std::string l_Actions = l_Input && l_Input.IsMap() ? ReadString(l_Input, "Actions", "") : std::string();
            if (const std::optional<UUID> l_Asset = UUID::Parse(l_Actions))
            {
                l_Project->m_InputActions = *l_Asset;
            }
            else if (!l_Actions.empty())
            {
                TR_CORE_WARN("Project: {} names its input actions '{}', which is not an asset's UUID, so it opens with none", l_Project->m_Name, l_Actions);
            }
        }

        if (l_Format >= 8)
        {
            SettingsYaml::ReadAudioSettings(l_Root["Audio"], l_Owner, l_Project->m_Audio);
        }

        if (!l_Project->IsCurrentEngineVersion())
        {
            TR_CORE_WARN("Project: {} was made with Trinity {}, and this is Trinity {}. It is opened as it is", l_Project->m_Name, l_Project->m_EngineVersion.empty() ? "of an unknown version" : l_Project->m_EngineVersion, GetVersionString());
        }

        TR_CORE_INFO("Project: opened {} from {}", l_Project->m_Name, ToUtf8(l_File));

        return l_Project;
    }

    Expected<void, std::string> Project::Save() const
    {
        YAML::Emitter l_Emitter;
        l_Emitter << YAML::BeginMap;
        l_Emitter << YAML::Key << "Format" << YAML::Value << c_FormatVersion;
        l_Emitter << YAML::Key << "Name" << YAML::Value << YAML::DoubleQuoted << m_Name;
        l_Emitter << YAML::Key << "EngineVersion" << YAML::Value << YAML::DoubleQuoted << m_EngineVersion;
        l_Emitter << YAML::Key << "StartScene" << YAML::Value << YAML::DoubleQuoted << (m_StartScene ? m_StartScene.ToString() : std::string());

        SettingsYaml::WritePhysicsSettings(l_Emitter, m_Physics);
        l_Emitter << YAML::Key << "Scripting" << YAML::Value << YAML::BeginMap;
        l_Emitter << YAML::Key << "Module" << YAML::Value << YAML::DoubleQuoted << m_ModuleName;
        l_Emitter << YAML::Key << "Assembly" << YAML::Value << YAML::DoubleQuoted << m_AssemblyName;
        l_Emitter << YAML::EndMap;
        l_Emitter << YAML::Key << "Input" << YAML::Value << YAML::BeginMap;
        l_Emitter << YAML::Key << "Actions" << YAML::Value << YAML::DoubleQuoted << (m_InputActions ? m_InputActions.ToString() : std::string());
        l_Emitter << YAML::EndMap;
        SettingsYaml::WriteAudioSettings(l_Emitter, m_Audio);
        l_Emitter << YAML::EndMap;

        const Expected<void, FileError> l_Written = FileSystem::WriteText(GetProjectFilePath(m_FilePath), std::string(l_Emitter.c_str()) + "\n");
        if (!l_Written)
        {
            return Unexpected{ std::format("{} could not be written: {}", ToUtf8(m_FilePath), ToString(l_Written.GetError())) };
        }

        return {};
    }

    bool Project::IsCurrentEngineVersion() const
    {
        return m_EngineVersion == GetVersionString();
    }

    // True once a format 1 start scene path is found in the registry and replaced by its UUID, after which the project should be saved
    bool Project::ResolveStartScenePath(const AssetRegistry& registry)
    {
        if (m_StartScenePath.empty())
        {
            return false;
        }

        const AssetRecord* l_Record = registry.FindByPath(m_StartScenePath);
        if (l_Record == nullptr)
        {
            TR_CORE_WARN("Project: the start scene {} named by {} is not in the project's assets", m_StartScenePath, m_Name);
            m_StartScenePath.clear();

            return false;
        }

        m_StartScene = l_Record->ID;
        m_StartScenePath.clear();

        return true;
    }

    std::filesystem::path Project::GetAssetsDirectory() const
    {
        return m_Directory / "Assets";
    }

    std::filesystem::path Project::GetSourceDirectory() const
    {
        return m_Directory / "Source";
    }

    std::filesystem::path Project::GetBuildDirectory() const
    {
        return m_Directory / "Build";
    }

    std::filesystem::path Project::GetScriptsDirectory() const
    {
        return m_Directory / "Scripts";
    }

    void Project::SetModuleName(std::string name)
    {
        TR_CORE_ASSERT(name.empty() || IsValidModuleName(name), "'{}' is not a name a module can have", name);

        m_ModuleName = std::move(name);
    }

    void Project::SetAssemblyName(std::string name)
    {
        TR_CORE_ASSERT(name.empty() || IsValidModuleName(name), "'{}' is not a name an assembly can have here", name);

        m_AssemblyName = std::move(name);
    }

    bool Project::IsValidModuleName(std::string_view name)
    {
        constexpr std::size_t c_MaxLength = 64;
        const auto a_IsLetter = [](char character) { return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z'); };
        const auto a_IsDigit = [](char character) { return character >= '0' && character <= '9'; };
        if (name.empty() || name.size() > c_MaxLength || !a_IsLetter(name.front()))
        {
            return false;
        }

        return std::ranges::all_of(name, [&](char character) { return a_IsLetter(character) || a_IsDigit(character) || character == '_'; });
    }

    std::string Project::MakeModuleName(std::string_view projectName)
    {
        // A digit or an underscore is kept once a letter has started the name
        std::string l_Name;
        for (const char it_Character : projectName)
        {
            const bool l_Letter = (it_Character >= 'A' && it_Character <= 'Z') || (it_Character >= 'a' && it_Character <= 'z');
            const bool l_Other = (it_Character >= '0' && it_Character <= '9') || it_Character == '_';
            if (l_Letter || (l_Other && !l_Name.empty()))
            {
                l_Name.push_back(it_Character);
            }
        }

        l_Name.resize(std::min<std::size_t>(l_Name.size(), 64));

        return IsValidModuleName(l_Name) ? l_Name : std::string("Game");
    }

    // Empty for a path outside Assets
    std::optional<std::string> Project::ToAssetPath(const std::filesystem::path& nativePath) const
    {
        std::error_code l_Error;
        const std::filesystem::path l_Assets = std::filesystem::weakly_canonical(GetAssetsDirectory(), l_Error);
        const std::filesystem::path l_Path = std::filesystem::weakly_canonical(nativePath, l_Error);
        const std::filesystem::path l_Relative = l_Path.lexically_relative(l_Assets);
        if (l_Error || l_Relative.empty() || *l_Relative.begin() == ".." || l_Relative == ".")
        {
            return std::nullopt;
        }

        const std::u8string l_Generic = l_Relative.generic_u8string();

        return std::format("{}/{}", c_AssetsMount, std::string_view(reinterpret_cast<const char*>(l_Generic.data()), l_Generic.size()));
    }

    // Under whichever of the project's mounts it names, so the project's own files, such as its scripts, are found as its assets are. A path naming no mount is taken as relative to Assets
    std::filesystem::path Project::ToNativePath(std::string_view assetPath) const
    {
        const auto a_Under = [assetPath](std::string_view mount) -> std::optional<std::string_view>
        {
            if (assetPath == mount)
            {
                return std::string_view();
            }

            if (assetPath.size() > mount.size() && assetPath.starts_with(mount) && assetPath[mount.size()] == '/')
            {
                return assetPath.substr(mount.size() + 1);
            }

            return std::nullopt;
        };

        if (const std::optional<std::string_view> l_Project = a_Under(c_ProjectMount))
        {
            return (m_Directory / FromUtf8(*l_Project)).make_preferred();
        }

        if (const std::optional<std::string_view> l_Cache = a_Under(c_CacheMount))
        {
            return (m_Directory / "Cache" / FromUtf8(*l_Cache)).make_preferred();
        }

        const std::optional<std::string_view> l_Asset = a_Under(c_AssetsMount);

        return (GetAssetsDirectory() / FromUtf8(l_Asset.value_or(assetPath))).make_preferred();
    }

    Expected<void, std::string> Project::Mount()
    {
        if (s_ProjectOpen)
        {
            return Unexpected{ std::string("another project is already open") };
        }

        // Cache holds only what Forge can make again, so git never keeps it, and a project cloned or unzipped without it gets an empty one instead of being refused
        std::error_code l_Error;
        if (std::filesystem::is_directory(m_Directory, l_Error))
        {
            std::filesystem::create_directories(m_Directory / "Cache", l_Error);
        }

        const bool l_Mounted = FileSystem::MountDirectory(c_ProjectMount, m_Directory, MountAccess::ReadWrite) && FileSystem::MountDirectory(c_AssetsMount, GetAssetsDirectory(), MountAccess::ReadWrite) && FileSystem::MountDirectory(c_CacheMount, m_Directory / "Cache", MountAccess::ReadWrite);
        if (!l_Mounted)
        {
            FileSystem::Unmount(c_CacheMount);
            FileSystem::Unmount(c_AssetsMount);
            FileSystem::Unmount(c_ProjectMount);

            return Unexpected{ std::format("{} could not be mounted, or has no Assets folder", ToUtf8(m_Directory)) };
        }

        m_Mounted = true;
        s_ProjectOpen = true;

        return {};
    }
}