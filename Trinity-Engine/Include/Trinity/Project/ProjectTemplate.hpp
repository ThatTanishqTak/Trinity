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
    // A project to start from, as Unity Hub's templates are: Templates/<Name> in a version's folder, holding a project's Assets, which a new project is given whole, and Template.yaml, which says what Hub's New Project shows of it, the project format its files were written at, and which of its assets are the project's start scene, its default input actions and its mixer. It holds no scripts, so it serves both languages. Read from the disk where it lies, so Hub reads any version's
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
        // A picture of its scene beside Template.yaml as a whole path, or empty for none
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

    // Refused for a folder without Template.yaml or Assets, a file of a newer format or written for a newer project format than this engine reads, or naming a start scene its Assets lack
    [[nodiscard]] TRINITY_API Expected<ProjectTemplate, std::string> ReadProjectTemplate(const std::filesystem::path& directory);
    // Each template in the Templates folder of the version whose folder is root, by name, each that cannot be read reported and left out
    [[nodiscard]] TRINITY_API std::vector<ProjectTemplate> FindProjectTemplates(const std::filesystem::path& root);
}