#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // Templates/<Name> in a version's folder: a project's Assets and Template.yaml, with no scripts
    struct ProjectTemplate
    {
        static constexpr std::uint32_t c_FormatVersion = 1;
        static constexpr std::string_view c_FileName = "Template.yaml";
        static constexpr std::string_view c_Folder = "Templates";
        static constexpr std::string_view c_AssetsFolder = "Assets";

        // The folder's name, which --template names it by
        std::string Name;
        std::filesystem::path Directory;
        // As Hub's New Project shows it
        std::string Title;
        std::string Description;
        // A whole path, or empty for none
        std::filesystem::path Thumbnail;
        std::uint32_t ProjectFormat = 0;
        // Each a path under Assets, as Scenes/Main.trscene, or empty for none
        std::string StartScene;
        std::string InputActions;
        std::string Mixer;
        // Whether Forge's Scene panel shows it in 3D at first
        bool View3D = false;

        [[nodiscard]] std::filesystem::path GetAssetsDirectory() const { return Directory / c_AssetsFolder; }
    };

    // projectFormat is the version's newest project format, or 0 for this engine's own
    [[nodiscard]] TRINITY_API Expected<ProjectTemplate, std::string> ReadProjectTemplate(const std::filesystem::path& directory, std::uint32_t projectFormat = 0);
    // By name. One that cannot be read is reported and left out
    [[nodiscard]] TRINITY_API std::vector<ProjectTemplate> FindProjectTemplates(const std::filesystem::path& root, std::uint32_t projectFormat = 0);
}