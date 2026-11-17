#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    struct ProjectListEntry
    {
        // The project's .trproj, as a whole path
        std::filesystem::path File;
        // As last listed, so a project whose folder is gone still shows what it was
        std::string Name;
        std::string EngineVersion;
        // In UTC, as 2026-11-07T05:41:29Z, or empty for one listed and not opened since
        std::string LastOpened;

        [[nodiscard]] bool operator==(const ProjectListEntry&) const = default;
    };

    // Projects.yaml in Hub's folder. Readers and writers hold Projects.yaml.lock, so no entry is lost
    namespace ProjectList
    {
        constexpr std::uint32_t c_FormatVersion = 1;
        constexpr std::string_view c_FileName = "Projects.yaml";

        // Hub in Trinity's folder of the user's data
        [[nodiscard]] TRINITY_API std::filesystem::path GetHubDirectory();

        // Opened ones newest first, then those never opened. A list not there yet is empty
        [[nodiscard]] TRINITY_API Expected<std::vector<ProjectListEntry>, std::string> Read(const std::filesystem::path& file);
        // Reads, changes and writes back while the lock is held
        [[nodiscard]] TRINITY_API Expected<void, std::string> Change(const std::filesystem::path& file, const std::function<void(std::vector<ProjectListEntry>&)>& change);
        // Lists it, or updates its name and version, as opened now
        [[nodiscard]] TRINITY_API Expected<void, std::string> RecordOpened(const std::filesystem::path& file, ProjectListEntry entry);
        // False when it was listed already, which leaves it as it was
        [[nodiscard]] TRINITY_API Expected<bool, std::string> Add(const std::filesystem::path& file, ProjectListEntry entry);
        [[nodiscard]] TRINITY_API Expected<void, std::string> Remove(const std::filesystem::path& file, const std::filesystem::path& project);

        // Compared on the disk where both exist, and by path otherwise
        [[nodiscard]] TRINITY_API bool IsSameFile(const std::filesystem::path& left, const std::filesystem::path& right);
    }
}