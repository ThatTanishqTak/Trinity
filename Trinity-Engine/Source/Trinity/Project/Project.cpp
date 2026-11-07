#include "Trinity/Project/Project.hpp"

#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Version.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/FileSystem/FileSystemUtilities.hpp"
#include "Trinity/Project/SettingsYaml.hpp"
#include "Trinity/Project/TargetPlatform.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <optional>
#include <span>
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

        // True or false, or nothing
        std::optional<bool> ReadBool(const YAML::Node & node)
        {
            const std::string l_Text = node && node.IsScalar() ? node.Scalar() : std::string();

            return l_Text == "true" || l_Text == "false" ? std::optional<bool>(l_Text == "true") : std::nullopt;
        }

        // The platform's APIs of those listed, in their order, each the platform lacks left out and reported, as a warning for one its own list names and as what happens for an older project's one list
        std::vector<GraphicsAPI> KeepPlatformAPIs(const TargetPlatform& platform, std::span<const GraphicsAPI> apis, std::string_view owner, bool older)
        {
            std::vector<GraphicsAPI> l_Kept;
            for (const GraphicsAPI it_API : apis)
            {
                if (std::ranges::find(platform.GraphicsAPIs, it_API) != platform.GraphicsAPIs.end())
                {
                    l_Kept.push_back(it_API);
                }
                else if (older)
                {
                    TR_CORE_INFO("{} listed {}, which {} games cannot draw with, so it builds for {} without it", owner, ToString(it_API), platform.DisplayName, platform.DisplayName);
                }
                else
                {
                    TR_CORE_WARN("{} lists {} for {}, which {} games cannot draw with, so it is left out", owner, ToString(it_API), platform.DisplayName, platform.DisplayName);
                }
            }

            return l_Kept;
        }

        // Format 10's map of each platform's order, or format 9's one list, which was the order on the platform the project now builds for
        void ReadGraphicsAPIs(const YAML::Node& node, std::uint32_t format, std::string_view owner, ExportSettings& settings, bool& unread)
        {
            if (!node)
            {
                return;
            }

            settings.GraphicsAPIs.clear();
            if (format < 10)
            {
                std::vector<GraphicsAPI> l_APIs;
                unread |= !SettingsYaml::ReadGraphicsAPIs(node, l_APIs);
                const TargetPlatform* l_Platform = TargetPlatforms::Find(settings.Platform);
                if (std::vector<GraphicsAPI> l_Kept = l_Platform != nullptr ? KeepPlatformAPIs(*l_Platform, l_APIs, owner, true) : std::vector<GraphicsAPI>(); !l_Kept.empty())
                {
                    settings.GraphicsAPIs.emplace(settings.Platform, std::move(l_Kept));
                }

                return;
            }

            unread |= !node.IsMap();
            for (auto it_Entry = node.begin(); node.IsMap() && it_Entry != node.end(); ++it_Entry)
            {
                const std::string l_Name = it_Entry->first.IsScalar() ? it_Entry->first.Scalar() : std::string();
                const TargetPlatform* l_Platform = TargetPlatforms::Find(l_Name);
                std::vector<GraphicsAPI> l_APIs;
                unread |= l_Platform == nullptr || !SettingsYaml::ReadGraphicsAPIs(it_Entry->second, l_APIs);
                if (l_Platform == nullptr)
                {
                    continue;
                }

                if (std::vector<GraphicsAPI> l_Kept = KeepPlatformAPIs(*l_Platform, l_APIs, owner, false); !l_Kept.empty())
                {
                    settings.GraphicsAPIs.insert_or_assign(std::string(l_Platform->Name), std::move(l_Kept));
                }
            }
        }

        // Each value that is missing keeps the one given, and each that cannot be read is reported and keeps it. A scene that cannot be one is left out, as is a scene listed again, which keeps its first place, and a platform Trinity has never heard of keeps the one given
        void ReadExportSettings(const YAML::Node& node, std::uint32_t format, std::string_view owner, ExportSettings& settings)
        {
            if (!node || !node.IsMap())
            {
                return;
            }

            bool l_Unread = false;
            if (const YAML::Node l_Scenes = node["Scenes"])
            {
                l_Unread |= !l_Scenes.IsSequence();
                settings.Scenes.clear();
                for (std::size_t it_Index = 0; l_Scenes.IsSequence() && it_Index < l_Scenes.size(); ++it_Index)
                {
                    const YAML::Node l_Entry = l_Scenes[it_Index];
                    const std::optional<UUID> l_Scene = l_Entry.IsMap() ? UUID::Parse(ReadString(l_Entry, "Scene", "")) : std::nullopt;
                    const std::optional<bool> l_Enabled = l_Entry.IsMap() && l_Entry["Enabled"] ? ReadBool(l_Entry["Enabled"]) : std::optional<bool>(true);
                    l_Unread |= !l_Scene || !l_Enabled;
                    if (l_Scene && std::ranges::find(settings.Scenes, *l_Scene, &ExportScene::Scene) == settings.Scenes.end())
                    {
                        settings.Scenes.push_back({ *l_Scene, l_Enabled.value_or(true) });
                    }
                }
            }

            settings.Product = ReadString(node, "Product", settings.Product);
            settings.Company = ReadString(node, "Company", settings.Company);
            settings.Version = ReadString(node, "Version", settings.Version);
            settings.Output = ReadString(node, "Output", settings.Output);
            const std::string l_Icon = ReadString(node, "Icon", "");
            const std::optional<UUID> l_IconID = UUID::Parse(l_Icon);
            l_Unread |= !l_Icon.empty() && !l_IconID;
            settings.Icon = l_Icon.empty() ? UUID() : l_IconID.value_or(settings.Icon);
            SettingsYaml::ReadWindowSettings(node["Window"], owner, settings.Window);
            ReadGraphicsAPIs(node["GraphicsAPIs"], format, owner, settings, l_Unread);

            if (const YAML::Node l_Development = node["Development"])
            {
                const std::optional<bool> l_Read = ReadBool(l_Development);
                l_Unread |= !l_Read;
                settings.Development = l_Read.value_or(settings.Development);
            }

            if (l_Unread)
            {
                TR_CORE_WARN("{} has an export setting that cannot be read, a scene, the platform, the icon, a graphics API or a value, which is left out or keeps its default", owner);
            }
        }

        void WriteExportSettings(YAML::Emitter & emitter, const ExportSettings & settings)
        {
            emitter << YAML::Key << "Export" << YAML::Value << YAML::BeginMap;
            emitter << YAML::Key << "Scenes" << YAML::Value << YAML::BeginSeq;
            for (const ExportScene& it_Scene : settings.Scenes)
            {
                emitter << YAML::Flow << YAML::BeginMap << YAML::Key << "Scene" << YAML::Value << YAML::DoubleQuoted << it_Scene.Scene.ToString() << YAML::Key << "Enabled" << YAML::Value << it_Scene.Enabled << YAML::EndMap;
            }

            emitter << YAML::EndSeq;
            emitter << YAML::Key << "Platform" << YAML::Value << settings.Platform;
            emitter << YAML::Key << "Product" << YAML::Value << YAML::DoubleQuoted << settings.Product;
            emitter << YAML::Key << "Company" << YAML::Value << YAML::DoubleQuoted << settings.Company;
            emitter << YAML::Key << "Version" << YAML::Value << YAML::DoubleQuoted << settings.Version;
            emitter << YAML::Key << "Icon" << YAML::Value << YAML::DoubleQuoted << (settings.Icon ? settings.Icon.ToString() : std::string());
            SettingsYaml::WriteWindowSettings(emitter, settings.Window);
            emitter << YAML::Key << "GraphicsAPIs" << YAML::Value;
            if (settings.GraphicsAPIs.empty())
            {
                emitter << YAML::Flow;
            }

            emitter << YAML::BeginMap;
            for (const auto& [it_Platform, it_APIs] : settings.GraphicsAPIs)
            {
                emitter << YAML::Key << it_Platform << YAML::Value << YAML::Flow << YAML::BeginSeq;
                for (const GraphicsAPI it_API : it_APIs)
                {
                    emitter << std::string(ToString(it_API));
                }

                emitter << YAML::EndSeq;
            }

            emitter << YAML::EndMap;
            emitter << YAML::Key << "Development" << YAML::Value << settings.Development;
            emitter << YAML::Key << "Output" << YAML::Value << YAML::DoubleQuoted << settings.Output;
            emitter << YAML::EndMap;
        }

        // The language format 10 names, or for an older file the one of a module and an assembly it has, and none for both or neither
        std::optional<ScriptLanguage> FindLanguage(std::uint32_t format, const YAML::Node& scripting, bool hasModule, bool hasAssembly)
        {
            if (format >= 10)
            {
                return scripting && scripting.IsMap() ? ParseScriptLanguage(ReadString(scripting, "Language", "")) : std::nullopt;
            }

            if (hasModule == hasAssembly)
            {
                return std::nullopt;
            }

            return hasModule ? ScriptLanguage::Cpp : ScriptLanguage::CSharp;
        }
    }

    std::vector<UUID> ExportSettings::GetBuildScenes() const
    {
        std::vector<UUID> l_Scenes;
        for (const ExportScene& it_Scene : Scenes)
        {
            if (it_Scene.Enabled)
            {
                l_Scenes.push_back(it_Scene.Scene);
            }
        }

        return l_Scenes;
    }

    std::vector<GraphicsAPI> ExportSettings::GetGraphicsAPIs() const
    {
        if (const auto a_Found = GraphicsAPIs.find(Platform); a_Found != GraphicsAPIs.end())
        {
            return a_Found->second;
        }

        const TargetPlatform* l_Platform = TargetPlatforms::Find(Platform);

        return l_Platform != nullptr ? std::vector<GraphicsAPI>(l_Platform->GraphicsAPIs.begin(), l_Platform->GraphicsAPIs.end()) : std::vector<GraphicsAPI>();
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
    Expected<Scope<Project>, std::string> Project::Create(const std::filesystem::path& directory, ScriptLanguage language)
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
        l_Project->m_Language = language;
        l_Project->m_Export.Product = l_Name;

        if (Expected<void, std::string> l_Mounted = l_Project->Mount(); !l_Mounted)
        {
            return Unexpected{ l_Mounted.GetError() };
        }

        if (Expected<void, std::string> l_Saved = l_Project->Save(); !l_Saved)
        {
            return Unexpected{ l_Saved.GetError() };
        }

        static_cast<void>(FileSystem::WriteText(std::format("{}/.gitignore", c_ProjectMount), "Cache/\nBuild/\nBuilds/\nSaved/\n"));

        TR_CORE_INFO("Project: created {} in {}, its scripts in {}", l_Project->m_Name, ToUtf8(l_Directory), language == ScriptLanguage::CSharp ? "C#" : "C++");

        return l_Project;
    }

    // A project file, or a folder holding exactly one
    Expected<std::filesystem::path, std::string> Project::FindFile(const std::filesystem::path& path)
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

        return l_File;
    }

    // Read as it lies on the disk, as Open reads it once mounted, for its format, name, version and scripting alone
    Expected<ProjectFileInfo, std::string> Project::ReadInfo(const std::filesystem::path& file)
    {
        constexpr std::uintmax_t c_Limit = 4 * 1024 * 1024;
        std::error_code l_Error;
        const std::uintmax_t l_Size = std::filesystem::file_size(file, l_Error);
        if (l_Error)
        {
            return Unexpected{ std::format("{} could not be read: {}", ToUtf8(file), l_Error.message()) };
        }

        if (l_Size > c_Limit)
        {
            return Unexpected{ std::format("{} is {} bytes, more than a project file can be", ToUtf8(file), l_Size) };
        }

        std::ifstream l_Stream(file, std::ios::binary);
        std::string l_Text(static_cast<std::size_t>(l_Size), '\0');
        l_Stream.read(l_Text.data(), static_cast<std::streamsize>(l_Text.size()));
        if (!l_Stream)
        {
            return Unexpected{ std::format("{} could not be read", ToUtf8(file)) };
        }

        YAML::Node l_Root;
        try
        {
            l_Root = YAML::Load(l_Text);
        }
        catch (const YAML::Exception& exception)
        {
            return Unexpected{ std::format("{} is not valid YAML: {}", ToUtf8(file), exception.what()) };
        }

        ProjectFileInfo l_Info;
        const std::string l_FormatText = l_Root.IsMap() ? ReadString(l_Root, "Format", "") : std::string();
        const std::from_chars_result l_Parsed = std::from_chars(l_FormatText.data(), l_FormatText.data() + l_FormatText.size(), l_Info.Format);
        if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_FormatText.data() + l_FormatText.size() || l_Info.Format == 0)
        {
            return Unexpected{ std::format("{} has no valid Format version", ToUtf8(file)) };
        }

        l_Info.Name = ReadString(l_Root, "Name", ToUtf8(file.stem()));
        l_Info.EngineVersion = ReadString(l_Root, "EngineVersion", "");
        const YAML::Node l_Scripting = l_Root["Scripting"];
        if (l_Info.Format >= 5 && l_Scripting && l_Scripting.IsMap())
        {
            l_Info.HasModule = !ReadString(l_Scripting, "Module", "").empty();
            l_Info.HasAssembly = l_Info.Format >= 6 && !ReadString(l_Scripting, "Assembly", "").empty();
        }

        l_Info.Language = FindLanguage(l_Info.Format, l_Scripting, l_Info.HasModule, l_Info.HasAssembly);

        return l_Info;
    }

    Expected<Scope<Project>, std::string> Project::Open(const std::filesystem::path& path, std::optional<ScriptLanguage> olderLanguage)
    {
        const Expected<std::filesystem::path, std::string> l_Found = FindFile(path);
        if (!l_Found)
        {
            return Unexpected{ l_Found.GetError() };
        }

        const std::filesystem::path& l_File = *l_Found;
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

        // Only the language's own module or assembly is named, so the other language's files are left on disk unbuilt
        const YAML::Node l_Scripting = l_Root["Scripting"];
        std::optional<ScriptLanguage> l_Language = FindLanguage(l_Format, l_Scripting, !l_Project->m_ModuleName.empty(), !l_Project->m_AssemblyName.empty());
        if (!l_Language && l_Format >= 10)
        {
            return Unexpected{ std::format("{} names no language its scripts are written in, CSharp or Cpp", ToUtf8(l_File)) };
        }

        if (!l_Language && !olderLanguage)
        {
            return Unexpected{ std::format("{} has {}, so it opens once the language it keeps is chosen", ToUtf8(l_File), l_Project->m_ModuleName.empty() ? "no C++ module and no C# project" : "a C++ module and a C# project") };
        }

        l_Project->m_Language = l_Language ? *l_Language : *olderLanguage;
        if (l_Project->m_Language == ScriptLanguage::CSharp && !l_Project->m_ModuleName.empty())
        {
            TR_CORE_INFO("Project: {}'s scripts are C#, so its C++ module {} is left in Source unbuilt", l_Project->m_Name, l_Project->m_ModuleName);
            l_Project->m_ModuleName.clear();
        }
        else if (l_Project->m_Language == ScriptLanguage::Cpp && !l_Project->m_AssemblyName.empty())
        {
            TR_CORE_INFO("Project: {}'s scripts are C++, so its C# project {} is left in Scripts unbuilt", l_Project->m_Name, l_Project->m_AssemblyName);
            l_Project->m_AssemblyName.clear();
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

        // An older project exports as a new one would, its name the product and its start scene the only scene of its build
        l_Project->m_Export.Product = l_Project->m_Name;
        if (l_Format >= 9)
        {
            ReadExportSettings(l_Root["Export"], l_Format, l_Owner, l_Project->m_Export);
        }
        else if (l_Project->m_StartScene)
        {
            l_Project->m_Export.Scenes.push_back({ l_Project->m_StartScene, true });
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
        l_Emitter << YAML::Key << "Language" << YAML::Value << std::string(ToString(m_Language));
        if (m_Language == ScriptLanguage::Cpp)
        {
            l_Emitter << YAML::Key << "Module" << YAML::Value << YAML::DoubleQuoted << m_ModuleName;
        }
        else
        {
            l_Emitter << YAML::Key << "Assembly" << YAML::Value << YAML::DoubleQuoted << m_AssemblyName;
        }

        l_Emitter << YAML::EndMap;
        l_Emitter << YAML::Key << "Input" << YAML::Value << YAML::BeginMap;
        l_Emitter << YAML::Key << "Actions" << YAML::Value << YAML::DoubleQuoted << (m_InputActions ? m_InputActions.ToString() : std::string());
        l_Emitter << YAML::EndMap;
        SettingsYaml::WriteAudioSettings(l_Emitter, m_Audio);
        WriteExportSettings(l_Emitter, m_Export);
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

    // True once a format 1 start scene path is found in the registry and replaced by its UUID, which becomes the only scene of its build, after which the project should be saved
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
        if (m_Export.Scenes.empty())
        {
            m_Export.Scenes.push_back({ m_StartScene, true });
        }

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

    std::filesystem::path Project::GetLockFile(const std::filesystem::path& directory)
    {
        return directory / "Cache" / "Forge.lock";
    }

    std::filesystem::path Project::GetScriptsDirectory() const
    {
        return m_Directory / "Scripts";
    }

    std::filesystem::path Project::GetExportDirectory() const
    {
        const std::filesystem::path l_Output = FromUtf8(m_Export.Output);

        return (l_Output.is_absolute() ? l_Output : m_Directory / l_Output).lexically_normal();
    }

    void Project::SetModuleName(std::string name)
    {
        TR_CORE_ASSERT(name.empty() || IsValidModuleName(name), "'{}' is not a name a module can have", name);
        TR_CORE_ASSERT(name.empty() || m_Language == ScriptLanguage::Cpp, "{}'s scripts are C#, so it has no C++ module", m_Name);

        m_ModuleName = std::move(name);
    }

    void Project::SetAssemblyName(std::string name)
    {
        TR_CORE_ASSERT(name.empty() || IsValidModuleName(name), "'{}' is not a name an assembly can have here", name);
        TR_CORE_ASSERT(name.empty() || m_Language == ScriptLanguage::CSharp, "{}'s scripts are C++, so it has no C# project", m_Name);

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