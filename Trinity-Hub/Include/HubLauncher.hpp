#pragma once

#include "HubInstalls.hpp"
#include "HubProjects.hpp"

#include <Trinity.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Opens projects in the Forge of their version, makes new ones, and runs a project's last export
class HubLauncher
{
public:
    // A Forge that fails to open or make its project ends within this. One ending later was closed
    static constexpr std::chrono::seconds c_StartWindow{ 10 };
    // Beside Hub's lists, for each Forge making a project, whose own folder must be new or empty
    static constexpr std::string_view c_LogsFolder = "Logs";

    // What came of something done or seen, for the page to show
    struct Report
    {
        std::string Message;
        bool Failed = false;
    };

    // The folder a new project is made in, which names it, its template or none, and its language
    struct Creation
    {
        std::filesystem::path Directory;
        std::string Template;
        Trinity::ScriptLanguage Language = Trinity::ScriptLanguage::CSharp;
    };

    explicit HubLauncher(std::filesystem::path hubDirectory);

    // The version's Forge, from its Release build, else Debug, else Distribution. Empty when none
    [[nodiscard]] static std::filesystem::path FindForge(const HubInstall& install);

    // In the version's Forge, unless a Forge has it open already, which is brought forward
    [[nodiscard]] Report Open(const HubProject& project, const HubInstall& install);
    // As --upgrade-project, which backs the project up first. language is for one older than format 10
    [[nodiscard]] Report Upgrade(const HubProject& project, const HubInstall& install, std::optional<Trinity::ScriptLanguage> language);
    // As --new-project, which writes every file in that version's formats and then opens it
    [[nodiscard]] Report Create(const Creation& creation, const HubInstall& install);
    // The game the project's last export made, from the folder it was exported to
    [[nodiscard]] static Report RunBuild(const HubProject& project);

    // Once a frame: each Forge that has ended is let go. Returns what one that failed early came to
    [[nodiscard]] std::optional<Report> Update();
    // A Forge Hub started has ended, or recorded an export, since this was last asked
    [[nodiscard]] bool TakeChanged();

private:
    // What a Forge was started to do
    enum class Purpose : std::uint8_t
    {
        Open,
        Upgrade,
        Create
    };

    struct Launched
    {
        std::filesystem::path ProjectFile;
        std::string Name;
        Trinity::StartedProcess Process;
        std::chrono::steady_clock::time_point StartedAt;
        std::filesystem::path Log;
        // When its log and the last export were written as it started, so an old log is not read
        std::optional<std::filesystem::file_time_type> LogTime;
        std::optional<std::filesystem::file_time_type> ExportTime;
        // Whether a new project's folder was not there before, so Hub may remove it if Forge fails
        Purpose Doing = Purpose::Open;
        bool RemoveOnFailure = false;
        // The version a project it moves named, so ending with it still named means the move failed
        std::string FromVersion;
    };

    // Open's and Upgrade's: the Forge started with options, then its log and Hub's folder
    [[nodiscard]] Report Start(const HubProject& project, const HubInstall& install, Purpose purpose, std::vector<std::string> options);
    [[nodiscard]] static std::string DescribeFailure(const Launched& launched, int exitCode);
    // A Forge started to move a project that ended while the project still names its old version
    [[nodiscard]] static bool IsUnmoved(const Launched& launched);
    // What a Forge that failed to make its project left, taken away and off the list if it was new
    [[nodiscard]] std::string RemoveFailedCreation(const Launched& launched) const;

    std::filesystem::path m_HubDirectory;
    std::vector<Launched> m_Launched;
    std::chrono::steady_clock::time_point m_NextLook;
    bool m_Changed = false;
};