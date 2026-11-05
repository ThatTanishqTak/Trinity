#pragma once

#include "Trinity/Audio/AudioSettings.hpp"
#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Screen.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"

#include <cstdint>
#include <filesystem>
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

    // What the project exports as a game, as Unity's Build Profiles and Player settings hold it: the scenes of its build in order, the ticked ones shipping and the first of those starting, the product, company and version its player is named for and keeps its saves and log under, its icon, one of the project's textures, the window it first opens with, the graphics APIs it tries in order, whether it is a Development Build, and the folder it is written to, from the project's folder unless it is a whole path
    struct TRINITY_API ExportSettings
    {
        static constexpr std::string_view c_DefaultCompany = "DefaultCompany";
        static constexpr std::string_view c_DefaultVersion = "1.0";
        static constexpr std::string_view c_DefaultOutput = "Builds";

        std::vector<ExportScene> Scenes;
        std::string Product;
        std::string Company{ c_DefaultCompany };
        std::string Version{ c_DefaultVersion };
        UUID Icon;
        WindowSettings Window;
        std::vector<GraphicsAPI> GraphicsAPIs{ GraphicsAPI::D3D12, GraphicsAPI::Vulkan };
        bool Development = false;
        std::string Output{ c_DefaultOutput };

        // The ticked scenes in order, the first the one the game starts with
        [[nodiscard]] std::vector<UUID> GetBuildScenes() const;

        [[nodiscard]] bool operator==(const ExportSettings&) const = default;
    };

    class TRINITY_API Project
    {
    public:
        // Format 3 added the collision layers, which an older project opens without, every pair colliding, format 4 each world's gravity and the step rate, which an older project opens with their defaults, format 5 the C++ game module, format 6 the C# assembly, format 7 the default input actions asset, which an older project has none of, format 8 the audio settings with the project's mixer, which an older project opens with their defaults and no mixer, and format 9 the export settings, which an older project opens with their defaults, its name for the product and its start scene the only scene of its build
        static constexpr std::uint32_t c_FormatVersion = 9;
        static constexpr std::string_view c_Extension = ".trproj";
        static constexpr std::string_view c_ProjectMount = "/project";
        static constexpr std::string_view c_AssetsMount = "/assets";
        static constexpr std::string_view c_CacheMount = "/cache";
        static constexpr std::string_view c_DefaultStartScene = "Scenes/Main.trscene";

        [[nodiscard]] static Expected<Scope<Project>, std::string> Create(const std::filesystem::path& directory);
        [[nodiscard]] static Expected<Scope<Project>, std::string> Open(const std::filesystem::path& path);

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

        // The C++ game module built from Source, by its target and library name, or empty for none. Saved with the project, which the caller does
        [[nodiscard]] const std::string& GetModuleName() const { return m_ModuleName; }
        void SetModuleName(std::string name);
        [[nodiscard]] bool HasModule() const { return !m_ModuleName.empty(); }

        // The C# assembly built from Scripts, by its project, assembly and root namespace name, or empty for none. Saved with the project, which the caller does
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
        PhysicsSettings m_Physics;
        std::string m_ModuleName;
        std::string m_AssemblyName;
        UUID m_InputActions;
        AudioSettings m_Audio;
        ExportSettings m_Export;
        bool m_Mounted = false;
    };
}