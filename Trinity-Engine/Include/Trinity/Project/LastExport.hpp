#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Trinity
{
    // The project's export that finished last, Cache/LastExport.yaml in its folder, which Forge writes as each export finishes and Hub reads from the disk, without opening the project, to run the game it made
    struct LastExport
    {
        static constexpr std::uint32_t c_FormatVersion = 1;
        static constexpr std::string_view c_FileName = "LastExport.yaml";

        // The game's executable as a whole path, in the folder it was exported to, which it is run from
        std::filesystem::path Executable;
        std::string Product;
        // With the profiler and the console, as a Development Build has them
        bool Development = false;
        // In UTC, as 2026-11-07T05:41:29Z
        std::string FinishedAt;

        [[nodiscard]] bool operator==(const LastExport&) const = default;
    };

    [[nodiscard]] TRINITY_API std::filesystem::path GetLastExportPath(const std::filesystem::path& projectDirectory);
    // Refused for a project with none yet, a file that is not YAML, of a newer format, or without an executable
    [[nodiscard]] TRINITY_API Expected<LastExport, std::string> ReadLastExport(const std::filesystem::path& projectDirectory);
    // As finished now, whatever FinishedAt it is given. Into a file beside it first, which then takes its place, so Hub never reads one half written
    [[nodiscard]] TRINITY_API Expected<void, std::string> WriteLastExport(const std::filesystem::path& projectDirectory, LastExport record);
}