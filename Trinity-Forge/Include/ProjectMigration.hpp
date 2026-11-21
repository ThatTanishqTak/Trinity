#pragma once

#include <Trinity.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

// As Unity asks before opening an older project: backed up whole, then saved in this one's formats
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

    // <Project>-<version>-<date>.zip beside the project's folder, in UTC, numbered when that is taken
    [[nodiscard]] std::filesystem::path GetBackupPath(const std::filesystem::path& directory, std::string_view version);
    // A stored zip, less Cache, Build and Builds. Written under another name, then renamed once whole
    [[nodiscard]] Trinity::Expected<void, std::string> BackUp(const std::filesystem::path& directory, const std::filesystem::path& zip);
    // Every asset this version writes, read and written again. One that cannot be read is logged
    [[nodiscard]] Report SaveAgain(Trinity::AssetRegistry& registry);
}