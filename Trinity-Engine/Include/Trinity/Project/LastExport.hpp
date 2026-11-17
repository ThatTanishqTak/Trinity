#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Trinity
{
    // Cache/LastExport.yaml: Forge writes it after each export, and Hub reads it to run the game
    struct LastExport
    {
        static constexpr std::uint32_t c_FormatVersion = 1;
        static constexpr std::string_view c_FileName = "LastExport.yaml";

        // A whole path
        std::filesystem::path Executable;
        std::string Product;
        // With the profiler and the console, as a Development Build has them
        bool Development = false;
        // In UTC, as 2026-11-07T05:41:29Z
        std::string FinishedAt;

        [[nodiscard]] bool operator==(const LastExport&) const = default;
    };

    [[nodiscard]] TRINITY_API std::filesystem::path GetLastExportPath(const std::filesystem::path& projectDirectory);
    // Refused for a project with none yet, or a file that is not YAML, newer or without an executable
    [[nodiscard]] TRINITY_API Expected<LastExport, std::string> ReadLastExport(const std::filesystem::path& projectDirectory);
    // Stamps FinishedAt itself. Written beside the file and renamed, so Hub never reads half of one
    [[nodiscard]] TRINITY_API Expected<void, std::string> WriteLastExport(const std::filesystem::path& projectDirectory, LastExport record);
}