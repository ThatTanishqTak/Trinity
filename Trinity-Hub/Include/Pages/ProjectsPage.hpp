#pragma once

#include "HubInstalls.hpp"
#include "HubPathPrompt.hpp"
#include "HubProjects.hpp"
#include "Pages/HubPage.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

// The person's projects from Projects.yaml, which every Forge adds to: each with its name, folder, language, the engine version it opens in with whether that version is installed, and when it was last opened, searched by name or folder and sorted by any column. Add lists a project already on the disk, by its folder or its .trproj, Remove from List takes one off and leaves its files as they are, and one whose folder is gone shows as Missing
class ProjectsPage final : public HubPage
{
public:
    ProjectsPage(HubProjects& projects, const HubInstalls& installs, HubPathPrompt& prompt);

    [[nodiscard]] std::string_view GetName() const override { return "Projects"; }
    [[nodiscard]] const char* GetIcon() const override;
    void OnShow() override;
    void Draw() override;

    // What Add does with the path chosen, a project's folder or its .trproj
    void AddProject(const std::filesystem::path& path);
    void RemoveProject(const std::filesystem::path& file);

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
    // The projects the search finds, in the table's order, made again only when the list, the search or the order changes
    void Order();
    [[nodiscard]] std::string DescribeState(const HubProject& project, bool& warning) const;
    void Report(std::string message, bool failed);

    HubProjects& m_Projects;
    const HubInstalls& m_Installs;
    HubPathPrompt& m_Prompt;

    std::string m_Search;
    std::vector<std::size_t> m_Order;
    std::uint64_t m_OrderedGeneration = std::numeric_limits<std::uint64_t>::max();
    std::string m_OrderedSearch;
    Column m_SortColumn = Column::LastOpened;
    bool m_SortAscending = false;
    bool m_Reorder = true;

    std::string m_Message;
    bool m_MessageFailed = false;
};