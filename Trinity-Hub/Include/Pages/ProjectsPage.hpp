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

// The person's projects from Projects.yaml, which every Forge adds to, searched and sorted
class ProjectsPage final : public HubPage
{
public:
    // launcher is shared with New Project. download and newProject are what those buttons do
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
    // In the version in that folder: the project's own, a newer one once asked, never an older one
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

    // What the upgrade popup asks: the project, the version, and the language to keep, if it must pick
    struct UpgradeQuestion
    {
        std::filesystem::path File;
        std::filesystem::path Install;
        std::optional<Trinity::ScriptLanguage> Language;
    };

    void DrawToolbar();
    void DrawList();
    // Open with Open With beside it, Run Build, Show in the file manager and Remove from List
    void DrawActions(const HubProject& project, std::optional<std::filesystem::path>& open, std::optional<Choice>& openWith, std::optional<std::filesystem::path>& remove);
    // The same on a right-click of the last item, which is the project's row
    void DrawRowMenu(const HubProject& project, std::optional<std::filesystem::path>& open, std::optional<Choice>& openWith, std::optional<std::filesystem::path>& remove);
    // Each usable version to open the project in: its own, a newer one, which asks, an older one greyed
    void DrawVersions(const HubProject& project, std::optional<Choice>& chosen) const;
    // When Open finds the project's version is not here: Download, or the versions here instead
    void DrawOfferPopup();
    // As Unity Hub asks before an older project opens in a newer editor. Cancel leaves it as it is
    void DrawUpgradePopup();
    [[nodiscard]] const HubProject* FindProject(const std::filesystem::path& file) const;
    // The projects the search finds, in the table's order, made again only when something changes
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