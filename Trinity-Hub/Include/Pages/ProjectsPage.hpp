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

// The person's projects from Projects.yaml, which every Forge adds to: each with its name, folder, language, the engine version it opens in with whether that version is installed, and when it was last opened, searched by name or folder and sorted by any column. Open opens one in a Forge of the version it names, or offers to download that version when it is not here, Run Build runs the game its last export made, Add lists a project already on the disk, by its folder or its .trproj, Remove from List takes one off and leaves its files as they are, and one whose folder is gone shows as Missing
class ProjectsPage final : public HubPage
{
public:
    // hubDirectory is the folder each Forge Hub starts adds to the list in, and download is what an offer's Download does with the release, which the Installs page installs
    ProjectsPage(HubProjects& projects, const HubInstalls& installs, HubReleases& releases, HubPathPrompt& prompt, std::filesystem::path hubDirectory, std::function<void(const HubRelease& release)> download);

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

    void DrawToolbar();
    void DrawList();
    // Open, Run Build, Show in the file manager and Remove from List, as a row's last column holds them
    void DrawActions(const HubProject& project, std::optional<std::filesystem::path>& open, std::optional<std::filesystem::path>& remove);
    // The version a project names, when Open finds it is not here: Download when GitHub has it, and the versions that are here, which it cannot open in yet
    void DrawOfferPopup();
    [[nodiscard]] const HubProject* FindProject(const std::filesystem::path& file) const;
    // The projects the search finds, in the table's order, made again only when the list, the search or the order changes
    void Order();
    [[nodiscard]] std::string DescribeState(const HubProject& project, bool& warning) const;
    void Report(std::string message, bool failed);

    HubProjects& m_Projects;
    const HubInstalls& m_Installs;
    HubReleases& m_Releases;
    HubPathPrompt& m_Prompt;
    HubLauncher m_Launcher;
    std::function<void(const HubRelease& release)> m_Download;

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
};