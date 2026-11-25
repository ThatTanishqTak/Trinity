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
    struct ProjectTemplate;

    struct ExportScene
    {
        UUID Scene;
        bool Enabled = true;

        [[nodiscard]] bool operator==(const ExportScene&) const = default;
    };

    // Edited by Forge's Build panel. Output is from the project's folder unless it is a whole path
    struct TRINITY_API ExportSettings
    {
        static constexpr std::string_view c_DefaultCompany = "DefaultCompany";
        static constexpr std::string_view c_DefaultVersion = "1.0";
        static constexpr std::string_view c_DefaultOutput = "Builds";

        std::vector<ExportScene> Scenes;
        // As TRINITY_PLATFORM names it. This engine's own until another is picked
        std::string Platform{ GetBuildPlatform() };
        std::string Product;
        std::string Company{ c_DefaultCompany };
        std::string Version{ c_DefaultVersion };
        UUID Icon;
        WindowSettings Window;
        // Only the platforms whose order was changed. The others take TargetPlatform's order
        std::map<std::string, std::vector<GraphicsAPI>, std::less<>> GraphicsAPIs;
        bool Development = false;
        std::string Output{ c_DefaultOutput };

        // The ticked scenes in order. The game starts with the first
        [[nodiscard]] std::vector<UUID> GetBuildScenes() const;
        // The order the game on Platform tries
        [[nodiscard]] std::vector<GraphicsAPI> GetGraphicsAPIs() const;

        [[nodiscard]] bool operator==(const ExportSettings&) const = default;
    };

    // Read without opening the project, as Hub lists projects. A newer format is given as it is
    struct ProjectFileInfo
    {
        std::uint32_t Format = 0;
        std::string Name;
        std::string EngineVersion;
        // None for a project older than format 10 with both a module and an assembly, or neither
        std::optional<ScriptLanguage> Language;
        bool HasModule = false;
        bool HasAssembly = false;

        [[nodiscard]] bool operator==(const ProjectFileInfo&) const = default;
    };

    // Older moves to this version once asked and backed up. Newer cannot be opened
    enum class ProjectAge : std::uint8_t
    {
        Current,
        Older,
        Newer
    };

    class TRINITY_API Project
    {
    public:
        // 3 layers, 4 gravity, 5 C++, 6 C#, 7 input actions, 8 audio, 9 export, 10 language and platform
        static constexpr std::uint32_t c_FormatVersion = 10;
        static constexpr std::string_view c_Extension = ".trproj";
        static constexpr std::string_view c_ProjectMount = "/project";
        static constexpr std::string_view c_AssetsMount = "/assets";
        static constexpr std::string_view c_CacheMount = "/cache";
        static constexpr std::string_view c_DefaultStartScene = "Scenes/Main.trscene";

        // With a template's assets when one is given
        [[nodiscard]] static Expected<Scope<Project>, std::string> Create(const std::filesystem::path& directory, ScriptLanguage language, const ProjectTemplate* projectTemplate = nullptr);
        // olderLanguage settles a project older than format 10 with both languages or neither
        [[nodiscard]] static Expected<Scope<Project>, std::string> Open(const std::filesystem::path& path, std::optional<ScriptLanguage> olderLanguage = std::nullopt);
        // A .trproj, or the one .trproj in a folder
        [[nodiscard]] static Expected<std::filesystem::path, std::string> FindFile(const std::filesystem::path& path);
        // Reads the disk without mounting anything. Refused only when unreadable or not a project's YAML
        [[nodiscard]] static Expected<ProjectFileInfo, std::string> ReadInfo(const std::filesystem::path& file);
        // A project that names no version is Older
        [[nodiscard]] static ProjectAge GetAge(const ProjectFileInfo& info);
        // Cache/Forge.lock, held by the Forge that has the project open, so a second finds the first
        [[nodiscard]] static std::filesystem::path GetLockFile(const std::filesystem::path& directory);
        // Cache/Thumbnail.png, a picture of the project's scene that Forge writes and Hub shows
        [[nodiscard]] static std::filesystem::path GetThumbnailFile(const std::filesystem::path& directory);

        ~Project();

        Project(const Project&) = delete;
        Project& operator=(const Project&) = delete;

        [[nodiscard]] Expected<void, std::string> Save() const;

        [[nodiscard]] const std::string& GetName() const { return m_Name; }
        [[nodiscard]] const std::string& GetEngineVersion() const { return m_EngineVersion; }
        [[nodiscard]] bool IsCurrentEngineVersion() const;
        // Save then writes this engine's version
        void SetCurrentEngineVersion() { m_EngineVersion = GetVersionString(); }

        [[nodiscard]] UUID GetStartScene() const { return m_StartScene; }
        void SetStartScene(UUID scene) { m_StartScene = scene; }
        [[nodiscard]] bool ResolveStartScenePath(const AssetRegistry& registry);

        // Saved with the project, which the caller does
        [[nodiscard]] const PhysicsSettings& GetPhysicsSettings() const { return m_Physics; }
        void SetPhysicsSettings(const PhysicsSettings& settings) { m_Physics = settings; }
        [[nodiscard]] const CollisionLayers& GetCollisionLayers() const { return m_Physics.Layers; }

        // Chosen as the project is made, and never changed
        [[nodiscard]] ScriptLanguage GetLanguage() const { return m_Language; }

        // Empty for none, as in a C# project. Saved with the project, which the caller does
        [[nodiscard]] const std::string& GetModuleName() const { return m_ModuleName; }
        void SetModuleName(std::string name);
        [[nodiscard]] bool HasModule() const { return !m_ModuleName.empty(); }

        // Empty for none, as in a C++ project. Saved with the project, which the caller does
        [[nodiscard]] const std::string& GetAssemblyName() const { return m_AssemblyName; }
        void SetAssemblyName(std::string name);
        [[nodiscard]] bool HasAssembly() const { return !m_AssemblyName.empty(); }

        // A letter, then letters, digits and underscores, 64 in all at most, so it is an identifier too
        [[nodiscard]] static bool IsValidModuleName(std::string_view name);

        // Saved with the project, which the caller does
        [[nodiscard]] const AudioSettings& GetAudioSettings() const { return m_Audio; }
        void SetAudioSettings(const AudioSettings& settings) { m_Audio = settings; }

        // Invalid for none. Saved with the project, which the caller does
        [[nodiscard]] UUID GetInputActions() const { return m_InputActions; }
        void SetInputActions(UUID asset) { m_InputActions = asset; }

        // Saved with the project, which the caller does
        [[nodiscard]] const ExportSettings& GetExportSettings() const { return m_Export; }
        void SetExportSettings(ExportSettings settings) { m_Export = std::move(settings); }
        // A whole path, from the project's folder unless it is one already
        [[nodiscard]] std::filesystem::path GetExportDirectory() const;
        // Leaves out what a module name cannot hold. Game when nothing is left
        [[nodiscard]] static std::string MakeModuleName(std::string_view projectName);

        [[nodiscard]] const std::filesystem::path& GetDirectory() const { return m_Directory; }
        [[nodiscard]] const std::filesystem::path& GetFilePath() const { return m_FilePath; }
        [[nodiscard]] std::filesystem::path GetAssetsDirectory() const;
        // Holds the module's sources, its CMakeLists.txt and a build tree for each configuration
        [[nodiscard]] std::filesystem::path GetSourceDirectory() const;
        [[nodiscard]] std::filesystem::path GetBuildDirectory() const;
        // Builds into Build/Scripts
        [[nodiscard]] std::filesystem::path GetScriptsDirectory() const;

        [[nodiscard]] std::optional<std::string> ToAssetPath(const std::filesystem::path& nativePath) const;
        // Takes a path under /assets, /project or /cache. One with none of them is relative to Assets
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