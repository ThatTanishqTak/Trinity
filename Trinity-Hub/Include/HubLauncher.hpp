#pragma once

#include "HubInstalls.hpp"
#include "HubProjects.hpp"

#include <Trinity.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Opens projects in Forge, each in the engine version it names, as Unity Hub opens each in its own editor, and runs the game a project's last export made. A Forge is started on the project, with its log in the project's Cache/Logs and Hub's folder for the projects list, and is kept hold of until it ends, so one that ends with an error in its first seconds, as one that cannot open its project does, is told of with what its log says
class HubLauncher
{
public:
    // A Forge that fails to open its project ends within this, and one ending later was closed, or failed at something else
    static constexpr std::chrono::seconds c_StartWindow{ 10 };

    // What came of something done or seen, for the page to show
    struct Report
    {
        std::string Message;
        bool Failed = false;
    };

    explicit HubLauncher(std::filesystem::path hubDirectory);

    // The version's Forge, from its Release build, else its Debug, else its Distribution, as an installed version holds Forge in Release alone. Empty when it has none
    [[nodiscard]] static std::filesystem::path FindForge(const HubInstall& install);

    // In the version's Forge, unless a Forge has it open already, which is brought forward
    [[nodiscard]] Report Open(const HubProject& project, const HubInstall& install);
    // The game the project's last export made, from the folder it was exported to
    [[nodiscard]] static Report RunBuild(const HubProject& project);

    // Once a frame: each Forge that has ended is let go of. What one that failed in its first seconds came to, when one did
    [[nodiscard]] std::optional<Report> Update();
    // A Forge Hub started has ended, or recorded an export, since this was last asked, so what the list shows may have changed
    [[nodiscard]] bool TakeChanged();

private:
    struct Launched
    {
        std::filesystem::path ProjectFile;
        std::string Name;
        Trinity::StartedProcess Process;
        std::chrono::steady_clock::time_point StartedAt;
        std::filesystem::path Log;
        // When its log and the project's last export were written as it started, so a log it never wrote is not read for its error, and an export it records is seen
        std::optional<std::filesystem::file_time_type> LogTime;
        std::optional<std::filesystem::file_time_type> ExportTime;
    };

    [[nodiscard]] static std::string DescribeFailure(const Launched& launched, int exitCode);

    std::filesystem::path m_HubDirectory;
    std::vector<Launched> m_Launched;
    std::chrono::steady_clock::time_point m_NextLook;
    bool m_Changed = false;
};