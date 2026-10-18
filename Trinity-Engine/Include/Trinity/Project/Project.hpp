#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Trinity
{
    class AssetRegistry;

    class TRINITY_API Project
    {
    public:
        // Format 3 added the collision layers, which an older project opens without, every pair colliding, format 4 each world's gravity and the step rate, which an older project opens with their defaults, format 5 the C++ game module, format 6 the C# assembly, and format 7 the default input actions asset, which an older project has none of
        static constexpr std::uint32_t c_FormatVersion = 7;
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

        // The input actions asset Play's scene reads, or invalid for none. Saved with the project, which the caller does
        [[nodiscard]] UUID GetInputActions() const { return m_InputActions; }
        void SetInputActions(UUID asset) { m_InputActions = asset; }
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
        bool m_Mounted = false;
    };
}