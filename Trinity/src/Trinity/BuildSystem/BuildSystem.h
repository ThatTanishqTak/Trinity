#pragma once

#include <filesystem>
#include <vector>
#include <string>

namespace Trinity
{
    struct BuildConfig
    {
        std::filesystem::path OutputDir;
        std::filesystem::path Executable;
        std::vector<std::filesystem::path> Scenes;
        std::vector<std::filesystem::path> Plugins;
        std::vector<std::string> TargetPlatforms;
    };

    class BuildSystem
    {
    public:
        static BuildConfig ParseBuildConfig(const std::filesystem::path& a_ConfigPath);
        void Build(const BuildConfig& a_Config);
        static void BuildPackage(const std::filesystem::path& outputDir);

    private:
        static std::vector<std::filesystem::path> GetReferencedAssets(const std::filesystem::path& a_ScenePath);
        void CompileAllShaders(const std::filesystem::path& outputDir);
    };
}