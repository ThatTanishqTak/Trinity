#pragma once

#include <Trinity.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

// Moving a project made with an older version of Trinity to this one, as Unity asks to before it opens a project of an older editor: the project backed up whole first, then every file this version writes saved again in its own formats
namespace ProjectMigration
{
    // The folders at a project's top the backup leaves out, as they are made again from the rest
    inline constexpr std::array<std::string_view, 3> c_LeftOut{ "Cache", "Build", "Builds" };

    // What saving the project's files again came to
    struct Report
    {
        // Each of these logged with its path
        std::size_t Rewritten = 0;
        std::size_t Failed = 0;
        // Already as this version writes them
        std::size_t Unchanged = 0;
    };

    // <Project>-<version>-<date>.zip beside the project's folder, its date today's in UTC, numbered as <Project>-<version>-<date> 2.zip when that is taken. version is the one the project names, or unknown for none
    [[nodiscard]] std::filesystem::path GetBackupPath(const std::filesystem::path& directory, std::string_view version);
    // The project's folder whole into a stored zip at the path, under a folder of its own name, but for the top's Cache, Build and Builds, so it unpacks beside the project as the project was. Written under another name first and renamed once whole, so a backup that fails leaves nothing
    [[nodiscard]] Trinity::Expected<void, std::string> BackUp(const std::filesystem::path& directory, const std::filesystem::path& zip);
    // Every scene, prefab, input actions asset, mixer and sound event the registry has, read and written again by this version's code, and each .meta of an older format written in this one. A file whose text this version writes the same is left as it is, and one that cannot be read is logged and left for its own page to say why
    [[nodiscard]] Report SaveAgain(Trinity::AssetRegistry& registry);
}