#pragma once

#include "HubInstalls.hpp"
#include "HubLauncher.hpp"
#include "HubPathPrompt.hpp"
#include "HubProjects.hpp"
#include "HubReleases.hpp"
#include "Pages/HubPage.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

// The person's projects from Projects.yaml, which every Forge adds to: each with its name, folder, language, the engine version it opens in with whether that version is installed, and when it was last opened, searched by name or folder and sorted by any column. New Project makes one, Open opens one in a Forge of the version it names, or offers to download that version when it is not here, Open With opens it in any version here, a newer one moving it to that version once asked and an older one never, Run Build runs the game its last export made, Add lists a project already on the disk, by its folder or its .trproj, Remove from List takes one off and leaves its files as they are, and one whose folder is gone shows as Missing
class ProjectsPage final : public HubPage
{
public:
    // launcher is shared with New Project, so each Forge either started is told of here as it ends. download is what an offer's Download does with the release, which the Installs page installs, and newProject what the New Project button does
    ProjectsPage(HubProjects& projects, const HubInstalls& installs, HubReleases& releases, HubPathPrompt& prompt, HubLauncher& launcher, std::function<void(const HubRelease& release)> download, std::function<void()> newProject);

    [[nodiscard]] std::string_view GetName() const override { return "Projects"; }
    [[nodiscard]] const char* GetIcon() const override;
    void OnShow() override;
    void Update() override;
    void Draw() override;

    // What Add does with the path chosen, a project's folder or its .trproj
    void AddProject(const std::filesystem::path& path);
    void RemoveProject(const std::filesystem::path& file);
    // In a Forge of the version the project names, or, with that version not here, an offer of it
    void OpenProject(const std::filesystem::path& file);
    // In the version in that folder, as Open With picks it: the one the project names as Open does, a newer one once the upgrade popup is answered, and never an older one
    void OpenWith(const std::filesystem::path& file, const std::filesystem::path& install);
    // What another page started, as New Project's Create, shown at the top of the list
    void ShowReport(HubLauncher::Report report);

private:
    // By the column the table sorts by
    enum class Column : std::uint32_t
    {
        Name,
        Language,
        Engine,
        State,
        LastOpened,
        Folder,
        Actions
    };

    // A project Open With chose a version for, by its file and the version's folder
    struct Choice
    {
        std::filesystem::path File;
        std::filesystem::path Install;
    };

    // What the upgrade popup asks before a newer version opens a project: the project, the version, and for one older than format 10 with both a C++ module and a C# project, or neither, the language it keeps, which must be picked
    struct UpgradeQuestion
    {
        std::filesystem::path File;
        std::filesystem::path Install;
        std::optional<Trinity::ScriptLanguage> Language;
    };

    void DrawToolbar();
    void DrawList();
    // Open with Open With beside it, Run Build, Show in the file manager and Remove from List, as a row's last column holds them
    void DrawActions(const HubProject& project, std::optional<std::filesystem::path>& open, std::optional<Choice>& openWith, std::optional<std::filesystem::path>& remove);
    // Each usable version, as Installs.yaml orders them, to open the project in: its own, a newer one, which asks first, and an older one greyed, as a project never moves to an older version
    void DrawVersions(const HubProject& project, std::optional<Choice>& chosen) const;
    // The version a project names, when Open finds it is not here: Download when GitHub has it, and the versions that are here to open it with instead
    void DrawOfferPopup();
    // As Unity Hub asks before an older project opens in a newer editor: both versions, the formats that change, the backup, and the language it keeps when it has both or neither. Cancel leaves it as it is
    void DrawUpgradePopup();
    [[nodiscard]] const HubProject* FindProject(const std::filesystem::path& file) const;
    // The projects the search finds, in the table's order, made again only when the list, the search or the order changes
    void Order();
    [[nodiscard]] std::string DescribeState(const HubProject& project, bool& warning) const;
    void Report(std::string message, bool failed);

    HubProjects& m_Projects;
    const HubInstalls& m_Installs;
    HubReleases& m_Releases;
    HubPathPrompt& m_Prompt;
    HubLauncher& m_Launcher;
    std::function<void(const HubRelease& release)> m_Download;
    std::function<void()> m_NewProject;

    std::string m_Search;
    std::vector<std::size_t> m_Order;
    std::uint64_t m_OrderedGeneration = std::numeric_limits<std::uint64_t>::max();
    std::string m_OrderedSearch;
    Column m_SortColumn = Column::LastOpened;
    bool m_SortAscending = false;
    bool m_Reorder = true;

    std::string m_Message;
    bool m_MessageFailed = false;

    // The project whose version the offer is of
    std::optional<std::filesystem::path> m_Offer;
    bool m_OpenOfferPopup = false;

    std::optional<UpgradeQuestion> m_Upgrade;
    bool m_OpenUpgradePopup = false;
};