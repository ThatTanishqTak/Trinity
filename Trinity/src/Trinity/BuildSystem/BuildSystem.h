#pragma once

#include <filesystem>

namespace Trinity
{
    struct BuildConfig
    {
        std::filesystem::path OutputDir;
    };

    class BuildSystem
    {
    public:
        void Build(const BuildConfig& a_Config);

    private:
        void CompileAllShaders(const std::filesystem::path& a_OutputDir);
    };
}