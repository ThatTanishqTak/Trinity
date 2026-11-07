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
    // A project of the list, as its last writer left it
    struct ProjectListEntry
    {
        // The project's .trproj, as a whole path
        std::filesystem::path File;
        // As the project file gave them when it was last listed, so a project whose folder is gone still shows what it was
        std::string Name;
        std::string EngineVersion;
        // In UTC, as 2026-11-07T05:41:29Z, or empty for one listed and not opened since
        std::string LastOpened;

        [[nodiscard]] bool operator==(const ProjectListEntry&) const = default;
    };

    // The person's projects, Projects.yaml in Hub's folder, which Hub shows and every Forge adds to as it opens or creates a project, as Unity Hub's list is. Each reader and writer holds Projects.yaml.lock beside it while it reads, merges and writes, so Hub and any number of Forges writing at once lose no entry. Each write replaces the file whole, so a reader never meets one half written, and a list that cannot be read, or that a newer Hub wrote, is left as it is
    namespace ProjectList
    {
        constexpr std::uint32_t c_FormatVersion = 1;
        constexpr std::string_view c_FileName = "Projects.yaml";

        // Hub's folder, Hub in Trinity's folder of the user's data, which holds the list and Hub's other files
        [[nodiscard]] TRINITY_API std::filesystem::path GetHubDirectory();

        // The opened ones newest first, then those never opened. A list not there yet is empty
        [[nodiscard]] TRINITY_API Expected<std::vector<ProjectListEntry>, std::string> Read(const std::filesystem::path& file);
        // The list as it is in the file, changed, then written back, all while the lock is held
        [[nodiscard]] TRINITY_API Expected<void, std::string> Change(const std::filesystem::path& file, const std::function<void(std::vector<ProjectListEntry>&)>& change);
        // Listed, or listed again with the name and version the entry gives, as opened now
        [[nodiscard]] TRINITY_API Expected<void, std::string> RecordOpened(const std::filesystem::path& file, ProjectListEntry entry);
        // Listed as it is given when it is not yet. False when it was listed already, and is left as it was
        [[nodiscard]] TRINITY_API Expected<bool, std::string> Add(const std::filesystem::path& file, ProjectListEntry entry);
        [[nodiscard]] TRINITY_API Expected<void, std::string> Remove(const std::filesystem::path& file, const std::filesystem::path& project);

        // Two project files are one where both are there and are one on the disk, and otherwise when their paths are
        [[nodiscard]] TRINITY_API bool IsSameFile(const std::filesystem::path& left, const std::filesystem::path& right);
    }
}