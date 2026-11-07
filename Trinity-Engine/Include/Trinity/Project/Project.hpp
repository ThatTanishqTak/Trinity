#pragma once

#include "Trinity/Audio/AudioSettings.hpp"
#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Screen.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Core/Version.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"
#include "Trinity/Scripting/ScriptField.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Trinity
{
    class AssetRegistry;

    // A scene of the build's list, by its asset, and whether it ships, as each scene of Unity's Scene List has a box
    struct ExportScene
    {
        UUID Scene;
        bool Enabled = true;

        [[nodiscard]] bool operator==(const ExportScene&) const = default;
    };

    // What the project exports as a game, as Unity's Build Profiles and Player settings hold it: the scenes of its build in order, the ticked ones shipping and the first of those starting, the platform it is built for, the product, company and version its player is named for and keeps its saves and log under, its icon, one of the project's textures, the window it first opens with, the graphics APIs it tries in order on each platform, whether it is a Development Build, and the folder it is written to, from the project's folder unless it is a whole path
    struct TRINITY_API ExportSettings
    {
        static constexpr std::string_view c_DefaultCompany = "DefaultCompany";
        static constexpr std::string_view c_DefaultVersion = "1.0";
        static constexpr std::string_view c_DefaultOutput = "Builds";

        std::vector<ExportScene> Scenes;
        // By the name TRINITY_PLATFORM gives it, the platform this engine runs on until another is picked
        std::string Platform{ GetBuildPlatform() };
        std::string Product;
        std::string Company{ c_DefaultCompany };
        std::string Version{ c_DefaultVersion };
        UUID Icon;
        WindowSettings Window;
        // Each platform's own order, as Unity keeps player settings for each, for the platforms whose order was changed. One without tries the APIs it has in TargetPlatform's order
        std::map<std::string, std::vector<GraphicsAPI>, std::less<>> GraphicsAPIs;
        bool Development = false;
        std::string Output{ c_DefaultOutput };

        // The ticked scenes in order, the first the one the game starts with
        [[nodiscard]] std::vector<UUID> GetBuildScenes() const;
        // The order the game on Platform tries
        [[nodiscard]] std::vector<GraphicsAPI> GetGraphicsAPIs() const;

        [[nodiscard]] bool operator==(const ExportSettings&) const = default;
    };

    // What a project file says of itself, read without opening the project, as Hub lists projects: its format, its name and engine version, its language, and whether it has a C++ module or a C# assembly. A format newer than this engine reads is given as it is, so the caller can say so
    struct ProjectFileInfo
    {
        std::uint32_t Format = 0;
        std::string Name;
        std::string EngineVersion;
        // The language its file names, or for a project older than format 10 the one of the two it has, and none for one with both or neither, whose language is asked for as it opens
        std::optional<ScriptLanguage> Language;
        bool HasModule = false;
        bool HasAssembly = false;

        [[nodiscard]] bool operator==(const ProjectFileInfo&) const = default;
    };

    class TRINITY_API Project
    {
    public:
        // Format 3 added the collision layers, which an older project opens without, every pair colliding, format 4 each world's gravity and the step rate, which an older project opens with their defaults, format 5 the C++ game module, format 6 the C# assembly, format 7 the default input actions asset, which an older project has none of, format 8 the audio settings with the project's mixer, which an older project opens with their defaults and no mixer, format 9 the export settings, which an older project opens with their defaults, its name for the product and its start scene the only scene of its build, and format 10 the project's one language and its target platform, each platform with its own graphics API order. An older project opens in the language of the module or assembly it has, or the one it is given when it has both or neither, keeping the other's name no more, and for the platform this engine runs on, with the graphics APIs it had that the platform has
        static constexpr std::uint32_t c_FormatVersion = 10;
        static constexpr std::string_view c_Extension = ".trproj";
        static constexpr std::string_view c_ProjectMount = "/project";
        static constexpr std::string_view c_AssetsMount = "/assets";
        static constexpr std::string_view c_CacheMount = "/cache";
        static constexpr std::string_view c_DefaultStartScene = "Scenes/Main.trscene";

        // In the language its scripts are written in, which it keeps
        [[nodiscard]] static Expected<Scope<Project>, std::string> Create(const std::filesystem::path& directory, ScriptLanguage language);
        // olderLanguage is the language a project older than format 10 with both a module and an assembly, or neither, takes, and without one such a project is refused, as ReadInfo tells beforehand
        [[nodiscard]] static Expected<Scope<Project>, std::string> Open(const std::filesystem::path& path, std::optional<ScriptLanguage> olderLanguage = std::nullopt);
        // The project file a path names, as a whole path: a .trproj, or the one .trproj in a folder
        [[nodiscard]] static Expected<std::filesystem::path, std::string> FindFile(const std::filesystem::path& path);
        // What the file says of the project, read from the disk without mounting anything, ignoring what it does not know. Refused only for a file that cannot be read or is not a project's YAML
        [[nodiscard]] static Expected<ProjectFileInfo, std::string> ReadInfo(const std::filesystem::path& file);
        // Cache/Forge.lock in the project's folder, which the Forge that has the project open holds, so a second Forge, or Hub, finds that one and brings it forward rather than opening the project twice
        [[nodiscard]] static std::filesystem::path GetLockFile(const std::filesystem::path& directory);

        ~Project();

        Project(const Project&) = delete;
        Project& operator=(const Project&) = delete;

        [[nodiscard]] Expected<void, std::string> Save() const;

        [[nodiscard]] const std::string& GetName() const { return m_Name; }
        [[nodiscard]] const std::string& GetEngineVersion() const { return m_EngineVersion; }
        [[nodiscard]] bool IsCurrentEngineVersion() const;

        [[nodiscard]] UUID GetStartScene() const { return m_StartScene; }
        void SetStartScene(UUID scene) { m_StartScene = scene; }
        [[nodiscard]] bool ResolveStartScenePath(const AssetRegistry& registry);

        // Saved with the project, which the caller does
        [[nodiscard]] const PhysicsSettings& GetPhysicsSettings() const { return m_Physics; }
        void SetPhysicsSettings(const PhysicsSettings& settings) { m_Physics = settings; }
        [[nodiscard]] const CollisionLayers& GetCollisionLayers() const { return m_Physics.Layers; }

        // The language every script of the project is written in, which only builds and loads, chosen as the project is made and never changed
        [[nodiscard]] ScriptLanguage GetLanguage() const { return m_Language; }

        // The C++ game module built from Source, by its target and library name, or empty for none, as a C# project always has. Saved with the project, which the caller does
        [[nodiscard]] const std::string& GetModuleName() const { return m_ModuleName; }
        void SetModuleName(std::string name);
        [[nodiscard]] bool HasModule() const { return !m_ModuleName.empty(); }

        // The C# assembly built from Scripts, by its project, assembly and root namespace name, or empty for none, as a C++ project always has. Saved with the project, which the caller does
        [[nodiscard]] const std::string& GetAssemblyName() const { return m_AssemblyName; }
        void SetAssemblyName(std::string name);
        [[nodiscard]] bool HasAssembly() const { return !m_AssemblyName.empty(); }

        // A name a module's target, library and C++ identifiers can all take: a letter, then letters, digits and underscores, up to 64 of them. A C# assembly's name is one too, so it is a namespace as well
        [[nodiscard]] static bool IsValidModuleName(std::string_view name);

        // The project's mixer, its voice limits, its Doppler factor and what occludes a sound. Saved with the project, which the caller does
        [[nodiscard]] const AudioSettings& GetAudioSettings() const { return m_Audio; }
        void SetAudioSettings(const AudioSettings& settings) { m_Audio = settings; }

        // The input actions asset Play's scene reads, or invalid for none. Saved with the project, which the caller does
        [[nodiscard]] UUID GetInputActions() const { return m_InputActions; }
        void SetInputActions(UUID asset) { m_InputActions = asset; }

        // What the project exports as a game, which Forge's Build panel edits. Saved with the project, which the caller does
        [[nodiscard]] const ExportSettings& GetExportSettings() const { return m_Export; }
        void SetExportSettings(ExportSettings settings) { m_Export = std::move(settings); }
        // The export's output folder as a whole path, from the project's folder unless it is one already
        [[nodiscard]] std::filesystem::path GetExportDirectory() const;
        // The project's name made into one, with what it cannot hold left out, or Game when nothing is left
        [[nodiscard]] static std::string MakeModuleName(std::string_view projectName);

        [[nodiscard]] const std::filesystem::path& GetDirectory() const { return m_Directory; }
        [[nodiscard]] const std::filesystem::path& GetFilePath() const { return m_FilePath; }
        [[nodiscard]] std::filesystem::path GetAssetsDirectory() const;
        // The module's sources and CMakeLists.txt, and its build trees, one for each configuration, which .gitignore leaves out
        [[nodiscard]] std::filesystem::path GetSourceDirectory() const;
        [[nodiscard]] std::filesystem::path GetBuildDirectory() const;
        // The C# project and its sources, which build into Build/Scripts
        [[nodiscard]] std::filesystem::path GetScriptsDirectory() const;

        [[nodiscard]] std::optional<std::string> ToAssetPath(const std::filesystem::path& nativePath) const;
        // A path under /assets, /project or /cache as the files where they lie. One with none of them is relative to Assets
        [[nodiscard]] std::filesystem::path ToNativePath(std::string_view assetPath) const;

    private:
        Project(std::filesystem::path directory, std::filesystem::path filePath);

        [[nodiscard]] Expected<void, std::string> Mount();

        std::filesystem::path m_Directory;
        std::filesystem::path m_FilePath;
        std::string m_Name;
        std::string m_EngineVersion;
        UUID m_StartScene;
        std::string m_StartScenePath;
        ScriptLanguage m_Language = ScriptLanguage::CSharp;
        PhysicsSettings m_Physics;
        std::string m_ModuleName;
        std::string m_AssemblyName;
        UUID m_InputActions;
        AudioSettings m_Audio;
        ExportSettings m_Export;
        bool m_Mounted = false;
    };
}