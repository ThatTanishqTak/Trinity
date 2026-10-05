#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Trinity
{
    class TRINITY_API Project
    {
    public:
        static constexpr std::uint32_t c_FormatVersion = 1;
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

        [[nodiscard]] const std::string& GetStartScene() const { return m_StartScene; }
        [[nodiscard]] std::string GetStartScenePath() const;
        void SetStartScene(std::string_view assetPath);

        [[nodiscard]] const std::filesystem::path& GetDirectory() const { return m_Directory; }
        [[nodiscard]] const std::filesystem::path& GetFilePath() const { return m_FilePath; }
        [[nodiscard]] std::filesystem::path GetAssetsDirectory() const;

        [[nodiscard]] std::optional<std::string> ToAssetPath(const std::filesystem::path& nativePath) const;
        [[nodiscard]] std::filesystem::path ToNativePath(std::string_view assetPath) const;

    private:
        Project(std::filesystem::path directory, std::filesystem::path filePath);

        [[nodiscard]] Expected<void, std::string> Mount();

        std::filesystem::path m_Directory;
        std::filesystem::path m_FilePath;
        std::string m_Name;
        std::string m_EngineVersion;
        std::string m_StartScene;
        bool m_Mounted = false;
    };
}