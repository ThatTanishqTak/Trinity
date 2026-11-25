#include "Pages/ProjectsPage.hpp"

#include "HubText.hpp"

#include <Trinity.hpp>
#include <Trinity/UI/Widgets.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <compare>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace
{
    constexpr std::array<Trinity::FileDialogFilter, 1> c_ProjectFilters{ { { "Trinity project", "trproj" } } };
    constexpr const char* c_OfferPopup = "Version Not Installed###HubOffer";
    constexpr const char* c_UpgradePopup = "Open in a Newer Version###HubUpgrade";
    constexpr const char* c_OpenWithPopup = "##OpenWith";
    constexpr const char* c_RowMenuPopup = "##RowMenu";
    // The room above and below what a project's row holds
    constexpr float c_RowPaddingInFonts = 0.5f;
    // A tile's width, which its picture's height follows at 16 to 9
    constexpr float c_TileWidthInFonts = 15.0f;
    // Lit to the tile's own edge, not half the spacing past it, which only ImGui's internal flag does
    constexpr ImGuiSelectableFlags c_TileFlags = ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick | static_cast<ImGuiSelectableFlags>(ImGuiSelectableFlags_NoPadWithHalfSpacing);

    // The files Forge saves again as it moves a project, by the format each manifest names for them
    struct FormatKind
    {
        std::string_view Name;
        std::uint32_t Trinity::EngineFormats::* Format;
    };

    constexpr std::array<FormatKind, 6> c_SavedAgain{ { { "scenes", &Trinity::EngineFormats::Scene },{ "prefabs", &Trinity::EngineFormats::Prefab },{ "input actions", &Trinity::EngineFormats::InputActions },{ "mixers", &Trinity::EngineFormats::AudioMixer },{ "sound events", &Trinity::EngineFormats::SoundEvent },{ ".meta files", &Trinity::EngineFormats::Meta } } };

    // How a version stands to a project: its own, newer, which moves it once asked, or older, never
    enum class VersionFit : std::uint8_t
    {
        Its,
        Newer,
        Older
    };


    std::string ToLower(std::string text)
    {
        std::ranges::transform(text, text.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });

        return text;
    }

    // A button that is only its icon, in the colour given, with no frame until the mouse is on it
    bool DrawIconButton(const char* label, const ImVec4& color)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        const bool l_Pressed = ImGui::Button(label);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        return l_Pressed;
    }

    std::string DescribeLanguage(const HubProject& project)
    {
        if (!project.Info)
        {
            return "-";
        }

        if (project.Info->Language)
        {
            return *project.Info->Language == Trinity::ScriptLanguage::CSharp ? "C#" : "C++";
        }

        // Older than format 10, so its language is asked for as it opens
        return project.Info->HasAssembly && project.Info->HasModule ? "C# and C++" : "None yet";
    }

    // Its project file is of a format newer than this Hub reads, which the version it names reads
    bool IsNewerProject(const HubProject& project)
    {
        return project.Info && project.Info->Format > Trinity::Project::c_FormatVersion;
    }

    // Why a project cannot be opened in any version, or empty when it can
    std::string_view GetOpenWithRefusal(const HubProject& project)
    {
        if (project.Missing)
        {
            return "Its folder is gone";
        }

        return project.Problem.empty() ? std::string_view() : "Its project file could not be read";
    }

    // Why a project cannot be opened in the version it names, or empty when it can
    std::string_view GetOpenRefusal(const HubProject& project)
    {
        if (const std::string_view l_Refusal = GetOpenWithRefusal(project); !l_Refusal.empty())
        {
            return l_Refusal;
        }

        return project.GetEngineVersion().empty() ? "Its project file names no engine version, so Open With picks the one it moves to" : std::string_view();
    }

    VersionFit GetFit(const HubProject& project, const HubInstall& install)
    {
        const std::uint32_t l_Reads = install.Manifest.Formats.Project;
        if (project.Info && l_Reads != 0 && project.Info->Format > l_Reads)
        {
            return VersionFit::Older;
        }

        if (project.GetEngineVersion().empty())
        {
            return VersionFit::Newer;
        }

        const std::strong_ordering l_Order = Trinity::CompareVersions(install.Manifest.Version, project.GetEngineVersion());

        return l_Order == 0 ? VersionFit::Its : l_Order > 0 ? VersionFit::Newer : VersionFit::Older;
    }

    // What choosing the version does, or why it cannot be chosen
    std::string DescribeFit(const HubProject& project, const HubInstall& install, VersionFit fit)
    {
        const std::string& l_Name = project.GetName();
        const std::string& l_Version = install.Manifest.Version;
        switch (fit)
        {
            case VersionFit::Its:
            {
                return std::format("Open {} in Trinity {}, the version it names", l_Name, l_Version);
            }
            case VersionFit::Newer:
            {
                const std::string l_From = project.GetEngineVersion().empty() ? std::string("names no version") : std::format("was made with Trinity {}", project.GetEngineVersion());

                return std::format("{} {}. Opening it here moves it to Trinity {}, which asks first, then backs it up and saves it in that version's formats", l_Name, l_From, l_Version);
            }
            default:
            {
                if (!project.GetEngineVersion().empty() && Trinity::CompareVersions(l_Version, project.GetEngineVersion()) < 0)
                {
                    return std::format("Trinity {} is older than Trinity {}, which {} was made with, and a project never moves to an older version", l_Version, project.GetEngineVersion(), l_Name);
                }

                return std::format("Trinity {} reads project format {} and older, and {} is of format {}, which a newer version wrote", l_Version, install.Manifest.Formats.Project, l_Name, project.Info ? project.Info->Format : 0);
            }
        }
    }

    // A build of the version the project names, whose manifest says its formats, or none
    const HubInstall* FindFormats(const HubInstalls& installs, std::string_view version)
    {
        const auto a_Install = std::ranges::find_if(installs.GetInstalls(), [version](const HubInstall& install)
        {
            return !version.empty() && install.Manifest.Formats.Project != 0 && Trinity::CompareVersions(install.Manifest.Version, version) == 0;
        });

        return a_Install != installs.GetInstalls().end() ? &*a_Install : nullptr;
    }

    // The project file's format from its file, and the others from the named version's manifest
    std::string DescribeFormatChanges(const HubProject& project, const HubInstall* from, const HubInstall& to)
    {
        const Trinity::EngineFormats& l_To = to.Manifest.Formats;
        std::string l_Changes;
        const auto l_Add = [&l_Changes](std::string_view name, std::uint32_t before, std::uint32_t after)
        {
            if (before != 0 && after != 0 && before != after)
            {
                l_Changes += std::format("{}{} from format {} to {}", l_Changes.empty() ? "" : ", ", name, before, after);
            }
        };

        l_Add("its project file", project.Info ? project.Info->Format : 0, l_To.Project);
        for (const FormatKind& it_Kind : c_SavedAgain)
        {
            l_Add(it_Kind.Name, from != nullptr ? from->Manifest.Formats.*it_Kind.Format : 0, l_To.*it_Kind.Format);
        }

        std::string l_Text = l_Changes.empty() ? std::string("No format of its files changes that Hub can see") : std::format("The formats that change: {}", l_Changes);
        if (from == nullptr)
        {
            l_Text += project.GetEngineVersion().empty() ? std::string(". It names no version, so Hub cannot say which formats its other files are in") : std::format(". No build of Trinity {} is here to say which formats its other files are in", project.GetEngineVersion());
        }

        return l_Text + ".";
    }
}

ProjectsPage::ProjectsPage(HubProjects& projects, const HubInstalls& installs, HubReleases& releases, HubSettings& settings, HubPathPrompt& prompt, HubLauncher& launcher, std::function<void(const HubRelease& release)> download, std::function<void()> newProject) : m_Projects(projects), m_Installs(installs), m_Releases(releases), m_Settings(settings), m_Prompt(prompt), m_Launcher(launcher), m_Download(std::move(download)), m_NewProject(std::move(newProject))
{

}

const char* ProjectsPage::GetIcon() const
{
    return Trinity::Icons::c_FolderOpen;
}

void ProjectsPage::OnShow()
{
    m_Projects.Refresh();
    m_Thumbnails.Recheck();
}

// A Forge started here that ended, or recorded an export, may have changed what the list shows
void ProjectsPage::Update()
{
    if (std::optional<HubLauncher::Report> l_Report = m_Launcher.Update())
    {
        Report(std::move(l_Report->Message), l_Report->Failed);
    }

    if (m_Launcher.TakeChanged())
    {
        m_Projects.Refresh();
    }

    m_Thumbnails.Update();
}

void ProjectsPage::Draw()
{
    m_Projects.Watch();
    HubPages::DrawTitle("Projects");
    DrawToolbar();

    if (m_Projects.IsFromNewerHub())
    {
        HubPages::DrawNeedsNewerHub(m_Projects.GetProblem());
    }
    else if (!m_Projects.GetProblem().empty())
    {
        HubPages::DrawWarning(m_Projects.GetProblem());
    }
    else if (const std::size_t l_Newer = static_cast<std::size_t>(std::ranges::count_if(m_Projects.GetProjects(), &IsNewerProject)); l_Newer > 0 && m_Message.empty())
    {
        HubPages::DrawNeedsNewerHub(l_Newer == 1 ? std::string("A project listed here is of a format a newer Trinity wrote, so what Hub shows of it may be wrong") : std::format("{} projects listed here are of a format a newer Trinity wrote, so what Hub shows of them may be wrong", l_Newer));
    }
    else if (!m_Message.empty())
    {
        if (ImGui::SmallButton(std::format("{}##Dismiss", Trinity::Icons::c_TimesCircle).c_str()))
        {
            m_Message.clear();
        }
        else
        {
            ImGui::SameLine();
            if (m_MessageFailed)
            {
                HubPages::DrawWarning(m_Message);
            }
            else
            {
                ImGui::TextWrapped("%s", m_Message.c_str());
            }
        }
    }

    ImGui::Spacing();
    if (m_Projects.GetProjects().empty())
    {
        HubPages::DrawEmpty("No projects yet", "Every project opened or created with Trinity is listed here. New Project makes one, and Add lists one already on disk.");

        return;
    }

    if (m_Settings.ProjectsGrid)
    {
        DrawGrid();
    }
    else
    {
        DrawList();
    }

    DrawOfferPopup();
    DrawUpgradePopup();
}

// Listed with what its file says, and as never opened, until a Forge opens it
void ProjectsPage::AddProject(const std::filesystem::path& path)
{
    const Trinity::Expected<std::filesystem::path, std::string> l_File = Trinity::Project::FindFile(path);
    if (!l_File)
    {
        Report(std::format("Nothing was added, as {}", l_File.GetError()), true);

        return;
    }

    const Trinity::Expected<Trinity::ProjectFileInfo, std::string> l_Info = Trinity::Project::ReadInfo(*l_File);
    if (!l_Info)
    {
        Report(std::format("Nothing was added, as {}", l_Info.GetError()), true);

        return;
    }

    const std::string l_Folder = HubText::ToUtf8(l_File->parent_path());
    const Trinity::Expected<bool, std::string> l_Added = Trinity::ProjectList::Add(m_Projects.GetFile(), { *l_File, l_Info->Name, l_Info->EngineVersion,{} });
    if (!l_Added)
    {
        Report(std::format("{} was not added, as {}", l_Info->Name, l_Added.GetError()), true);

        return;
    }

    Report(*l_Added ? std::format("Added {} from {}", l_Info->Name, l_Folder) : std::format("{} in {} is listed already", l_Info->Name, l_Folder), false);
    m_Projects.Refresh();
}

void ProjectsPage::RemoveProject(const std::filesystem::path& file)
{
    const HubProject* l_Project = FindProject(file);
    const std::string l_Name = l_Project != nullptr ? l_Project->GetName() : HubText::ToUtf8(file.stem());
    if (const Trinity::Expected<void, std::string> l_Removed = Trinity::ProjectList::Remove(m_Projects.GetFile(), file); !l_Removed)
    {
        Report(std::format("{} was not taken off the list, as {}", l_Name, l_Removed.GetError()), true);

        return;
    }

    // Off the list is off the favorites too, so one added again later starts as any other
    if (m_Settings.IsFavorite(file))
    {
        m_Settings.SetFavorite(file, false);
        m_Settings.Save();
    }

    const std::string l_Folder = HubText::ToUtf8(file.parent_path());
    Report(l_Project != nullptr && l_Project->Missing ? std::format("Took {} off the list, whose folder {} is gone", l_Name, l_Folder) : std::format("Took {} off the list, and left its files in {} as they are", l_Name, l_Folder), false);
    m_Projects.Refresh();
}

void ProjectsPage::SetFavorite(const std::filesystem::path& file, bool favorite)
{
    m_Settings.SetFavorite(file, favorite);
    m_Settings.Save();
    m_Reorder = true;
}

void ProjectsPage::OpenProject(const std::filesystem::path& file)
{
    const HubProject* l_Project = FindProject(file);
    if (l_Project == nullptr || !GetOpenRefusal(*l_Project).empty())
    {
        return;
    }

    if (const HubInstall* l_Install = m_Installs.FindUsable(l_Project->GetEngineVersion()))
    {
        HubLauncher::Report l_Report = m_Launcher.Open(*l_Project, *l_Install);
        Report(std::move(l_Report.Message), l_Report.Failed);

        return;
    }

    // GitHub asked again, at most once a minute, so a release made since Hub last asked is offered
    m_Releases.Refresh();
    m_Offer = file;
    m_OpenOfferPopup = true;
}

// A version greyed in the list never gets here, though one chosen as the list changed is refused
void ProjectsPage::OpenWith(const std::filesystem::path& file, const std::filesystem::path& install)
{
    const HubProject* l_Project = FindProject(file);
    const HubInstall* l_Install = m_Installs.Find(install);
    if (l_Project == nullptr || l_Install == nullptr || !HubInstalls::IsUsable(*l_Install) || !GetOpenWithRefusal(*l_Project).empty())
    {
        return;
    }

    switch (const VersionFit l_Fit = GetFit(*l_Project, *l_Install))
    {
        case VersionFit::Its:
        {
            HubLauncher::Report l_Report = m_Launcher.Open(*l_Project, *l_Install);
            Report(std::move(l_Report.Message), l_Report.Failed);

            break;
        }
        case VersionFit::Newer:
        {
            m_Upgrade = UpgradeQuestion{ file, install, l_Project->Info ? l_Project->Info->Language : std::nullopt };
            m_OpenUpgradePopup = true;

            break;
        }
        default:
        {
            Report(std::format("{} was not opened, as {}", l_Project->GetName(), DescribeFit(*l_Project, *l_Install, l_Fit)), true);

            break;
        }
    }
}

// New Project first, as Unity Hub's is, then Add, the search and the count, and List or Grid at the end
void ProjectsPage::DrawToolbar()
{
    if (ImGui::Button(std::format("{} New Project", Trinity::Icons::c_Plus).c_str()))
    {
        m_NewProject();
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Make a project from one of a version's templates, in its own Forge");
    }

    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Add", Trinity::Icons::c_FolderOpen).c_str()))
    {
        ImGui::OpenPopup("##AddProject");
    }

    if (ImGui::BeginPopup("##AddProject"))
    {
        if (ImGui::MenuItem(std::format("{} Project Folder...", Trinity::Icons::c_Folder).c_str()))
        {
            m_Prompt.Ask({ HubPathPrompt::Kind::Folder, "Add a project: the folder holding its .trproj",{},{}, [this](const std::filesystem::path& path) { AddProject(path); } });
        }

        if (ImGui::MenuItem(std::format("{} Project File...", Trinity::Icons::c_File).c_str()))
        {
            m_Prompt.Ask({ HubPathPrompt::Kind::OpenFile, "Add a project: its .trproj", c_ProjectFilters,{}, [this](const std::filesystem::path& path) { AddProject(path); } });
        }

        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
    Trinity::Widgets::SearchField("##Search", "Search by name or folder", m_Search);

    const std::size_t l_Total = m_Projects.GetProjects().size();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (m_OrderedGeneration == m_Projects.GetGeneration() && !m_OrderedSearch.empty())
    {
        ImGui::TextDisabled("%s", std::format("{} of {} project{}", m_Order.size(), l_Total, l_Total == 1 ? "" : "s").c_str());
    }
    else
    {
        ImGui::TextDisabled("%s", std::format("{} project{}", l_Total, l_Total == 1 ? "" : "s").c_str());
    }

    // Against the right edge, or after the count where the window is too narrow for that
    const float l_Spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float l_Width = ImGui::CalcTextSize(Trinity::Icons::c_List).x + ImGui::CalcTextSize(Trinity::Icons::c_Grid).x + ImGui::GetStyle().FramePadding.x * 4.0f + l_Spacing;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - l_Width));
    std::optional<bool> l_Grid;
    if (Trinity::Widgets::ToggleButton(std::format("{}##List", Trinity::Icons::c_List).c_str(), !m_Settings.ProjectsGrid))
    {
        l_Grid = false;
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("List");
    }

    ImGui::SameLine(0.0f, l_Spacing);
    if (Trinity::Widgets::ToggleButton(std::format("{}##Grid", Trinity::Icons::c_Grid).c_str(), m_Settings.ProjectsGrid))
    {
        l_Grid = true;
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Grid");
    }

    if (l_Grid && *l_Grid != m_Settings.ProjectsGrid)
    {
        m_Settings.ProjectsGrid = *l_Grid;
        m_Settings.Save();
    }
}

void ProjectsPage::DrawList()
{
    constexpr ImGuiTableFlags c_Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_Sortable;
    if (!ImGui::BeginTable("##Projects", 7, c_Flags))
    {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 0.0f, static_cast<ImGuiID>(Column::Name));
    ImGui::TableSetupColumn("Language", ImGuiTableColumnFlags_None, 0.0f, static_cast<ImGuiID>(Column::Language));
    ImGui::TableSetupColumn("Engine", ImGuiTableColumnFlags_None, 0.0f, static_cast<ImGuiID>(Column::Engine));
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_NoSort, 0.0f, static_cast<ImGuiID>(Column::State));
    ImGui::TableSetupColumn("Last Opened", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 0.0f, static_cast<ImGuiID>(Column::LastOpened));
    ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 0.0f, static_cast<ImGuiID>(Column::Folder));
    ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_NoSort, 0.0f, static_cast<ImGuiID>(Column::Actions));

    if (ImGuiTableSortSpecs* l_Specs = ImGui::TableGetSortSpecs(); l_Specs != nullptr && l_Specs->SpecsDirty)
    {
        if (l_Specs->SpecsCount > 0)
        {
            m_SortColumn = static_cast<Column>(l_Specs->Specs[0].ColumnUserID);
            m_SortAscending = l_Specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
        }

        l_Specs->SpecsDirty = false;
        m_Reorder = true;
    }

    Order();
    ImGui::TableHeadersRow();

    // The rows below the header are taller: the table puts this much above and below what a cell holds
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const float l_RowPadding = std::round(ImGui::GetFontSize() * c_RowPaddingInFonts);
    const float l_RowHeight = ImGui::GetFrameHeight() + l_RowPadding * 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(l_Style.CellPadding.x, l_RowPadding));

    const std::vector<HubProject>& l_Projects = m_Projects.GetProjects();
    Requests l_Requests;
    ImGuiListClipper l_Clipper;
    l_Clipper.Begin(static_cast<int>(m_Order.size()), l_RowHeight);
    while (l_Clipper.Step())
    {
        for (int it_Row = l_Clipper.DisplayStart; it_Row < l_Clipper.DisplayEnd; ++it_Row)
        {
            const std::size_t l_Index = m_Order[static_cast<std::size_t>(it_Row)];
            const HubProject& l_Project = l_Projects[l_Index];
            const bool l_Favorite = m_Favorite[l_Index];
            ImGui::PushID(it_Row);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, l_RowHeight);

            // Under the whole row and the buttons at its end, which keep their clicks. A Selectable reaches half the item spacing past its size, so this spacing takes it over the row's padding
            ImGui::TableNextColumn();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(l_Style.ItemSpacing.x, l_RowPadding * 2.0f));
            const bool l_Pressed = ImGui::Selectable("##Row", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, ImGui::GetFrameHeight()));
            ImGui::PopStyleVar();

            // A double-click opens it, and so does Enter, which presses the row with no mouse button let go. One click alone does nothing
            if (l_Pressed && (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || !ImGui::IsMouseReleased(ImGuiMouseButton_Left)))
            {
                RequestOpen(l_Project, l_Requests);
            }

            DrawRowMenu(l_Project, l_Favorite, l_Requests);
            ImGui::SameLine(0.0f, 0.0f);
            if (DrawStar(l_Favorite))
            {
                l_Requests.Favorite = l_Project.Entry.File;
            }

            ImGui::SameLine(0.0f, l_Style.ItemInnerSpacing.x);
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
            if (l_Project.Missing)
            {
                ImGui::TextDisabled("%s", l_Project.GetName().c_str());
            }
            else
            {
                ImGui::TextUnformatted(l_Project.GetName().c_str());
            }

            ImGui::PopFont();

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(DescribeLanguage(l_Project).c_str());

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(l_Project.GetEngineVersion().empty() ? "-" : l_Project.GetEngineVersion().c_str());

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            bool l_Warning = false;
            const std::string l_State = DescribeState(l_Project, l_Warning);
            if (l_Warning)
            {
                HubPages::DrawWarning(l_State);
            }
            else
            {
                ImGui::TextDisabled("%s", l_State.c_str());
            }

            if (!l_Project.Problem.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                ImGui::SetTooltip("%s", l_Project.Problem.c_str());
            }
            else if (l_Project.Problem.empty() && IsNewerProject(l_Project) && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                ImGui::SetTooltip("%s", std::format("Its project file is of format {}, and this Hub reads format {} and older", l_Project.Info->Format, Trinity::Project::c_FormatVersion).c_str());
            }

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (const std::optional<std::chrono::sys_seconds> l_Opened = HubText::ParseTime(l_Project.Entry.LastOpened))
            {
                ImGui::TextUnformatted(HubText::DescribeAge(*l_Opened).c_str());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                {
                    ImGui::SetTooltip("%s", HubText::DescribeTime(*l_Opened).c_str());
                }
            }
            else
            {
                ImGui::TextDisabled("Never");
            }

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            HubPages::DrawClipped(HubText::ToUtf8(l_Project.Entry.File.parent_path()));

            ImGui::TableNextColumn();
            DrawActions(l_Project, l_Requests);
            ImGui::PopID();
        }
    }

    ImGui::PopStyleVar();
    ImGui::EndTable();

    if (m_Order.empty() && !m_OrderedSearch.empty())
    {
        ImGui::TextDisabled("%s", std::format("No project's name or folder holds \"{}\"", m_Search).c_str());
    }

    Apply(l_Requests);
}

void ProjectsPage::DrawGrid()
{
    Order();
    if (!ImGui::BeginChild("##ProjectsGrid"))
    {
        ImGui::EndChild();

        return;
    }

    if (m_Order.empty() && !m_OrderedSearch.empty())
    {
        ImGui::TextDisabled("%s", std::format("No project's name or folder holds \"{}\"", m_Search).c_str());
    }

    // A tile is its picture, then a line for its name and its buttons, then a line for what stands out
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const float l_Padding = l_Style.FramePadding.x;
    const float l_Width = std::round(ImGui::GetFontSize() * c_TileWidthInFonts);
    const float l_PictureHeight = std::round((l_Width - l_Padding * 2.0f) * 9.0f / 16.0f);
    const ImVec2 l_Size(l_Width, l_PictureHeight + ImGui::GetFrameHeight() + ImGui::GetTextLineHeight() + l_Padding * 3.0f);
    const std::size_t l_PerRow = static_cast<std::size_t>(std::max(1.0f, (ImGui::GetContentRegionAvail().x + l_Style.ItemSpacing.x) / (l_Width + l_Style.ItemSpacing.x)));
    const std::size_t l_Rows = (m_Order.size() + l_PerRow - 1) / l_PerRow;

    const std::vector<HubProject>& l_Projects = m_Projects.GetProjects();
    Requests l_Requests;
    ImGuiListClipper l_Clipper;
    l_Clipper.Begin(static_cast<int>(l_Rows), l_Size.y + l_Style.ItemSpacing.y);
    while (l_Clipper.Step())
    {
        for (int it_Row = l_Clipper.DisplayStart; it_Row < l_Clipper.DisplayEnd; ++it_Row)
        {
            // Each tile is put in its place, as on the same line as the last it would sit as low as that one's buttons
            const ImVec2 l_RowStart = ImGui::GetCursorScreenPos();
            const std::size_t l_First = static_cast<std::size_t>(it_Row) * l_PerRow;
            for (std::size_t it_Place = l_First; it_Place < std::min(l_First + l_PerRow, m_Order.size()); ++it_Place)
            {
                const std::size_t l_Index = m_Order[it_Place];
                ImGui::SetCursorScreenPos(ImVec2(l_RowStart.x + static_cast<float>(it_Place - l_First) * (l_Width + l_Style.ItemSpacing.x), l_RowStart.y));
                ImGui::PushID(static_cast<int>(it_Place));
                DrawTile(l_Projects[l_Index], m_Favorite[l_Index], l_Size, l_Requests);
                ImGui::PopID();
            }
        }
    }

    ImGui::EndChild();
    Apply(l_Requests);
}

// One selectable under the whole tile, drawn over, with its star and its menu's button on top of it
void ProjectsPage::DrawTile(const HubProject& project, bool favorite, const ImVec2& size, Requests& requests)
{
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const float l_Padding = l_Style.FramePadding.x;
    const float l_PictureHeight = size.y - ImGui::GetFrameHeight() - ImGui::GetTextLineHeight() - l_Padding * 3.0f;
    const ImVec2 l_Start = ImGui::GetCursorScreenPos();

    // A group, so the buttons placed inside the tile leave what follows it where the tile ends
    ImGui::BeginGroup();
    const bool l_Pressed = ImGui::Selectable("##Tile", false, c_TileFlags, size);
    if (l_Pressed && (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || !ImGui::IsMouseReleased(ImGuiMouseButton_Left)))
    {
        RequestOpen(project, requests);
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        const std::string l_Folder = HubText::ToUtf8(project.Entry.File.parent_path());
        ImGui::SetTooltip("%s", project.Problem.empty() ? l_Folder.c_str() : std::format("{}\n{}", l_Folder, project.Problem).c_str());
    }

    DrawRowMenu(project, favorite, requests);

    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    l_DrawList.AddRect(l_Start, ImVec2(l_Start.x + size.x, l_Start.y + size.y), ImGui::GetColorU32(ImGuiCol_Border), l_Style.FrameRounding);
    const ImTextureData* l_Picture = project.Missing ? nullptr : m_Thumbnails.Get(Trinity::Project::GetThumbnailFile(project.Entry.File.parent_path()));
    Trinity::Tools::Thumbnails::Draw(l_Picture, Trinity::Icons::c_Cube, ImVec2(l_Start.x + l_Padding, l_Start.y + l_Padding), ImVec2(l_Start.x + size.x - l_Padding, l_Start.y + l_Padding + l_PictureHeight));

    // The star and the menu's button end the name's line, and the name is cut where they begin
    const float l_NameTop = l_Start.y + l_PictureHeight + l_Padding * 2.0f;
    const float l_ButtonsWidth = ImGui::CalcTextSize(Trinity::Icons::c_Star).x + ImGui::CalcTextSize(Trinity::Icons::c_EllipsisVertical).x + l_Style.FramePadding.x * 4.0f;
    const float l_ButtonsLeft = l_Start.x + size.x - l_Padding - l_ButtonsWidth;
    ImGui::SetCursorScreenPos(ImVec2(l_ButtonsLeft, l_NameTop));
    if (DrawStar(favorite))
    {
        requests.Favorite = project.Entry.File;
    }

    ImGui::SameLine(0.0f, 0.0f);
    if (DrawIconButton(std::format("{}##More", Trinity::Icons::c_EllipsisVertical).c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text)))
    {
        ImGui::OpenPopup(c_RowMenuPopup);
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("More");
    }

    ImGui::EndGroup();

    const ImVec2 l_NameAt(l_Start.x + l_Padding, l_NameTop + l_Style.FramePadding.y);
    l_DrawList.PushClipRect(l_NameAt, ImVec2(l_ButtonsLeft, l_Start.y + size.y), true);
    l_DrawList.AddText(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), ImGui::GetFontSize(), l_NameAt, ImGui::GetColorU32(project.Missing ? ImGuiCol_TextDisabled : ImGuiCol_Text), project.GetName().c_str());
    l_DrawList.PopClipRect();

    // What is wrong with it where something is, and its version and when it was last opened where not
    bool l_Warning = false;
    std::string l_Detail = DescribeState(project, l_Warning);
    if (!l_Warning)
    {
        const std::optional<std::chrono::sys_seconds> l_Opened = HubText::ParseTime(project.Entry.LastOpened);
        l_Detail = std::format("Trinity {}, {}", project.GetEngineVersion(), l_Opened ? HubText::DescribeAge(*l_Opened) : std::string("never opened"));
    }

    const ImVec2 l_DetailAt(l_Start.x + l_Padding, l_NameTop + ImGui::GetFrameHeight());
    const ImU32 l_DetailColor = l_Warning ? Trinity::Theme::GetColorU32(Trinity::ThemeColor::Warning) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
    l_DrawList.PushClipRect(l_DetailAt, ImVec2(l_Start.x + size.x - l_Padding, l_Start.y + size.y), true);
    l_DrawList.AddText(l_DetailAt, l_DetailColor, l_Detail.c_str());
    l_DrawList.PopClipRect();
}

bool ProjectsPage::DrawStar(bool favorite)
{
    const bool l_Pressed = DrawIconButton(std::format("{}##Star", favorite ? Trinity::Icons::c_Star : Trinity::Icons::c_StarOutline).c_str(), favorite ? Trinity::Theme::GetColor(Trinity::ThemeColor::Accent) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("%s", favorite ? "Remove from Favorites" : "Add to Favorites, which are kept first");
    }

    return l_Pressed;
}

// Once the rows are drawn, as each of these changes the list or opens a popup
void ProjectsPage::Apply(const Requests& requests)
{
    if (requests.Open)
    {
        OpenProject(*requests.Open);
    }

    if (requests.OpenWith)
    {
        OpenWith(requests.OpenWith->File, requests.OpenWith->Install);
    }

    if (requests.Remove)
    {
        RemoveProject(*requests.Remove);
    }

    if (requests.Favorite)
    {
        SetFavorite(*requests.Favorite, !m_Settings.IsFavorite(*requests.Favorite));
    }
}

void ProjectsPage::RequestOpen(const HubProject& project, Requests& requests)
{
    if (const std::string_view l_Refusal = GetOpenRefusal(project); l_Refusal.empty())
    {
        requests.Open = project.Entry.File;
    }
    else
    {
        Report(std::string(l_Refusal), true);
    }
}

// Each button says in its tooltip what it does, or why it cannot
void ProjectsPage::DrawActions(const HubProject& project, Requests& requests)
{
    constexpr ImGuiHoveredFlags c_Tooltip = ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled;

    const std::string_view l_Refusal = GetOpenRefusal(project);
    ImGui::BeginDisabled(!l_Refusal.empty());
    if (ImGui::Button("Open"))
    {
        requests.Open = project.Entry.File;
    }

    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(c_Tooltip))
    {
        ImGui::SetTooltip("%s", l_Refusal.empty() ? std::format("Open in Forge, in Trinity {}", project.GetEngineVersion()).c_str() : std::string(l_Refusal).c_str());
    }

    const std::string_view l_WithRefusal = GetOpenWithRefusal(project);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::BeginDisabled(!l_WithRefusal.empty());
    if (ImGui::Button(std::format("{}##OpenWith", Trinity::Icons::c_ChevronDown).c_str()))
    {
        ImGui::OpenPopup(c_OpenWithPopup);
    }

    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(c_Tooltip))
    {
        ImGui::SetTooltip("%s", l_WithRefusal.empty() ? "Open With: one of the versions here, its own or a newer one, which moves it to that version" : std::string(l_WithRefusal).c_str());
    }

    if (ImGui::BeginPopup(c_OpenWithPopup))
    {
        ImGui::TextDisabled("Open With");
        ImGui::Separator();
        DrawVersions(project, requests.OpenWith);
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!project.Build);
    if (ImGui::Button(Trinity::Icons::c_Gamepad))
    {
        HubLauncher::Report l_Report = HubLauncher::RunBuild(project);
        Report(std::move(l_Report.Message), l_Report.Failed);
    }

    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(c_Tooltip))
    {
        if (project.Build)
        {
            const std::optional<std::chrono::sys_seconds> l_Finished = HubText::ParseTime(project.Build->FinishedAt);
            ImGui::SetTooltip("%s", std::format("Run Build: {}{}{}", project.Build->Product, project.Build->Development ? ", a Development Build" : "", l_Finished ? std::format(", exported {}", HubText::DescribeAge(*l_Finished)) : std::string()).c_str());
        }
        else
        {
            ImGui::SetTooltip("Run Build: none yet, until an export from Forge's Build panel finishes");
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(project.Missing);
    if (ImGui::Button(Trinity::Icons::c_FolderOpen))
    {
        if (const Trinity::Expected<void, std::string> l_Opened = Trinity::Process::OpenWithSystem(project.Entry.File.parent_path()); !l_Opened)
        {
            Report(l_Opened.GetError(), true);
        }
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Show in the file manager");
    }

    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(Trinity::Icons::c_TimesCircle))
    {
        requests.Remove = project.Entry.File;
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Remove from List, leaving its files as they are");
    }
}

// What the row's buttons do, by name, for those who right-click a row before they read its icons
void ProjectsPage::DrawRowMenu(const HubProject& project, bool favorite, Requests& requests)
{
    if (!ImGui::BeginPopupContextItem(c_RowMenuPopup))
    {
        return;
    }

    if (ImGui::MenuItem("Open", nullptr, false, GetOpenRefusal(project).empty()))
    {
        requests.Open = project.Entry.File;
    }

    if (ImGui::BeginMenu("Open With", GetOpenWithRefusal(project).empty()))
    {
        DrawVersions(project, requests.OpenWith);
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Run Build", nullptr, false, project.Build.has_value()))
    {
        HubLauncher::Report l_Report = HubLauncher::RunBuild(project);
        Report(std::move(l_Report.Message), l_Report.Failed);
    }

    ImGui::Separator();
    if (ImGui::MenuItem(favorite ? "Remove from Favorites" : "Add to Favorites"))
    {
        requests.Favorite = project.Entry.File;
    }

    if (ImGui::MenuItem("Show in the File Manager", nullptr, false, !project.Missing))
    {
        if (const Trinity::Expected<void, std::string> l_Opened = Trinity::Process::OpenWithSystem(project.Entry.File.parent_path()); !l_Opened)
        {
            Report(l_Opened.GetError(), true);
        }
    }

    if (ImGui::MenuItem("Remove from List"))
    {
        requests.Remove = project.Entry.File;
    }

    ImGui::EndPopup();
}

// The offer closes by itself once the list is read again without the project
void ProjectsPage::DrawOfferPopup()
{
    if (std::exchange(m_OpenOfferPopup, false))
    {
        ImGui::OpenPopup(c_OfferPopup);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(c_OfferPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    const HubProject* l_Project = m_Offer ? FindProject(*m_Offer) : nullptr;
    if (l_Project == nullptr)
    {
        m_Offer.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();

        return;
    }

    const std::string& l_Version = l_Project->GetEngineVersion();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(std::format("{} opens in Trinity {}, which is not installed here.", l_Project->GetName(), l_Version).c_str());
    ImGui::Spacing();

    const auto a_Release = std::ranges::find_if(m_Releases.GetReleases(), [&l_Version](const HubRelease& release) { return release.Version == l_Version; });
    if (a_Release != m_Releases.GetReleases().end())
    {
        if (ImGui::Button(std::format("{} Download Trinity {}", Trinity::Icons::c_Download, l_Version).c_str()))
        {
            m_Download(*a_Release);
            m_Offer.reset();
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("%s", std::format("{} from GitHub, {}, installed on the Installs page", a_Release->ZipName, Trinity::Memory::FormatBytes(a_Release->ZipSize)).c_str());
        }
    }
    else if (m_Releases.IsFetching())
    {
        ImGui::TextDisabled("Asking GitHub for Trinity's releases...");
    }
    else
    {
        const std::string l_Why = m_Releases.GetNote().empty() ? std::format("No release of Trinity {} for {} is on GitHub.", l_Version, Trinity::GetBuildPlatform()) : std::format("Hub could not ask GitHub for Trinity's releases: {}", m_Releases.GetNote());
        ImGui::TextDisabled("%s", std::format("{} A build of it can be listed with Locate on the Installs page.", l_Why).c_str());
    }

    HubPages::DrawSection("Open With");
    std::optional<Choice> l_Chosen;
    DrawVersions(*l_Project, l_Chosen);

    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape) || l_Chosen)
    {
        m_Offer.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();

    // Once the offer is closed, as a newer version opens the upgrade popup in its place
    if (l_Chosen)
    {
        OpenWith(l_Chosen->File, l_Chosen->Install);
    }
}

void ProjectsPage::DrawVersions(const HubProject& project, std::optional<Choice>& chosen) const
{
    bool l_Any = false;
    for (const HubInstall& it_Install : m_Installs.GetInstalls())
    {
        if (!HubInstalls::IsUsable(it_Install))
        {
            continue;
        }

        l_Any = true;
        const VersionFit l_Fit = GetFit(project, it_Install);
        const std::string_view l_Source = it_Install.Source == HubInstall::Origin::Installed ? "Installed" : "Located";
        const std::string_view l_Stands = l_Fit == VersionFit::Its ? "its version" : l_Fit == VersionFit::Newer ? "newer, moves it" : "older";
        ImGui::BeginDisabled(l_Fit == VersionFit::Older);
        if (ImGui::Selectable(std::format("{}   {}, {}##{}", HubInstalls::Describe(it_Install.Manifest), l_Source, l_Stands, HubText::ToUtf8(it_Install.Directory)).c_str()))
        {
            chosen = Choice{ project.Entry.File, it_Install.Directory };
        }

        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", std::format("{}\n{}", DescribeFit(project, it_Install, l_Fit), HubText::ToUtf8(it_Install.Directory)).c_str());
        }
    }

    if (!l_Any)
    {
        ImGui::TextDisabled("No version is installed here");
    }
}

// What Back Up and Open starts is told of on the page, and a failed move as its Forge ends
void ProjectsPage::DrawUpgradePopup()
{
    if (std::exchange(m_OpenUpgradePopup, false))
    {
        ImGui::OpenPopup(c_UpgradePopup);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(c_UpgradePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    const HubProject* l_Project = m_Upgrade ? FindProject(m_Upgrade->File) : nullptr;
    const HubInstall* l_Install = m_Upgrade ? m_Installs.Find(m_Upgrade->Install) : nullptr;
    if (l_Project == nullptr || l_Install == nullptr || !l_Project->Info)
    {
        m_Upgrade.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();

        return;
    }

    const Trinity::ProjectFileInfo& l_Info = *l_Project->Info;
    const std::string l_Old = l_Project->GetEngineVersion().empty() ? std::string("an unknown version") : std::format("Trinity {}", l_Project->GetEngineVersion());
    const std::string& l_New = l_Install->Manifest.Version;
    const bool l_AskLanguage = !l_Info.Language;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
    ImGui::TextUnformatted(std::format("{} was made with {}. Opening it in {} moves it to that version:", l_Project->GetName(), l_Old, HubInstalls::Describe(l_Install->Manifest)).c_str());
    ImGui::Spacing();
    // Bullet and text rather than BulletText, which does not wrap
    ImGui::Bullet();
    ImGui::TextUnformatted("It is backed up first, as a zip beside its folder, holding everything but Cache, Build and Builds.");
    ImGui::Bullet();
    ImGui::TextUnformatted(std::format("{} Every scene, prefab and other asset is saved again in Trinity {}'s formats, and its scripts are built again.", DescribeFormatChanges(*l_Project, FindFormats(m_Installs, l_Project->GetEngineVersion()), *l_Install), l_New).c_str());
    ImGui::Bullet();
    ImGui::TextUnformatted(std::format("{} cannot open it afterwards.", l_Old).c_str());
    if (l_AskLanguage)
    {
        ImGui::Spacing();
        ImGui::TextUnformatted(std::format("It was made before a project had one language, and has {}. Which language does it keep?", l_Info.HasModule ? "both a C++ module and a C# project" : "no scripts yet").c_str());
        ImGui::TextDisabled(l_Info.HasModule ? "It cannot change later. The other language's files stay on disk, unbuilt, and its scripts in scenes do not run." : "It cannot change later.");
        if (ImGui::RadioButton("C#", m_Upgrade->Language == Trinity::ScriptLanguage::CSharp))
        {
            m_Upgrade->Language = Trinity::ScriptLanguage::CSharp;
        }

        ImGui::SameLine();
        if (ImGui::RadioButton("C++", m_Upgrade->Language == Trinity::ScriptLanguage::Cpp))
        {
            m_Upgrade->Language = Trinity::ScriptLanguage::Cpp;
        }
    }

    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    ImGui::BeginDisabled(!m_Upgrade->Language);
    const bool l_Move = ImGui::Button("Back Up and Open");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled) && !m_Upgrade->Language)
    {
        ImGui::SetTooltip("Pick the language it keeps first");
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        Report(std::format("{} was left as it is, as moving it to Trinity {} was declined", l_Project->GetName(), l_New), false);
        m_Upgrade.reset();
        ImGui::CloseCurrentPopup();
    }
    else if (l_Move)
    {
        HubLauncher::Report l_Report = m_Launcher.Upgrade(*l_Project, *l_Install, l_AskLanguage ? m_Upgrade->Language : std::nullopt);
        Report(std::move(l_Report.Message), l_Report.Failed);
        m_Upgrade.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

const HubProject* ProjectsPage::FindProject(const std::filesystem::path& file) const
{
    const auto a_Project = std::ranges::find_if(m_Projects.GetProjects(), [&file](const HubProject& project) { return project.Entry.File == file; });

    return a_Project != m_Projects.GetProjects().end() ? &*a_Project : nullptr;
}

// Names and folders were made lowercase as the list was read, so a search is one look into each
void ProjectsPage::Order()
{
    const std::string l_Search = ToLower(m_Search);
    if (!m_Reorder && m_OrderedGeneration == m_Projects.GetGeneration() && l_Search == m_OrderedSearch)
    {
        return;
    }

    // The list read again is often a Forge that closed, which may have written its project's picture
    if (m_OrderedGeneration != m_Projects.GetGeneration())
    {
        m_Thumbnails.Recheck();
    }

    const std::vector<HubProject>& l_Projects = m_Projects.GetProjects();
    m_Order.clear();
    m_Favorite.assign(l_Projects.size(), false);
    for (std::size_t it_Index = 0; it_Index < l_Projects.size(); ++it_Index)
    {
        m_Favorite[it_Index] = m_Settings.IsFavorite(l_Projects[it_Index].Entry.File);
        if (l_Search.empty() || l_Projects[it_Index].SearchText.find(l_Search) != std::string::npos)
        {
            m_Order.push_back(it_Index);
        }
    }

    const Column l_Column = m_SortColumn;
    const bool l_Ascending = m_SortAscending;
    std::ranges::stable_sort(m_Order, [&l_Projects, &l_Favorite = std::as_const(m_Favorite), l_Column, l_Ascending](std::size_t left, std::size_t right)
    {
        // Favorites stay first whichever way the column sorts
        if (l_Favorite[left] != l_Favorite[right])
        {
            return l_Favorite[left];
        }

        const HubProject& l_Left = l_Projects[l_Ascending ? left : right];
        const HubProject& l_Right = l_Projects[l_Ascending ? right : left];
        switch (l_Column)
        {
            case Column::Language:
            {
                return DescribeLanguage(l_Left) < DescribeLanguage(l_Right);
            }
            case Column::Engine:
            {
                return Trinity::CompareVersions(l_Left.GetEngineVersion(), l_Right.GetEngineVersion()) < 0;
            }
            case Column::LastOpened:
            {
                return l_Left.Entry.LastOpened < l_Right.Entry.LastOpened;
            }
            case Column::Folder:
            {
                return l_Left.Entry.File.parent_path() < l_Right.Entry.File.parent_path();
            }
            default:
            {
                return l_Left.SearchText < l_Right.SearchText;
            }
        }
    });

    m_OrderedGeneration = m_Projects.GetGeneration();
    m_OrderedSearch = l_Search;
    m_Reorder = false;
}

// Missing first, then an unreadable or newer file, then how its engine version stands here
std::string ProjectsPage::DescribeState(const HubProject& project, bool& warning) const
{
    warning = true;
    if (project.Missing)
    {
        return "Missing";
    }

    if (!project.Problem.empty())
    {
        return "Unreadable";
    }

    if (IsNewerProject(project))
    {
        return "Needs a newer Hub";
    }

    const std::string& l_Version = project.GetEngineVersion();
    if (l_Version.empty())
    {
        return "Names no version";
    }

    const HubInstall* l_Newest = nullptr;
    for (const HubInstall& it_Install : m_Installs.GetInstalls())
    {
        if (!HubInstalls::IsUsable(it_Install))
        {
            continue;
        }

        if (it_Install.Manifest.Version == l_Version)
        {
            warning = false;

            return "Installed";
        }

        if (l_Newest == nullptr || Trinity::CompareVersions(it_Install.Manifest.Version, l_Newest->Manifest.Version) > 0)
        {
            l_Newest = &it_Install;
        }
    }

    if (l_Newest != nullptr && Trinity::CompareVersions(l_Version, l_Newest->Manifest.Version) > 0)
    {
        return "Newer than any installed";
    }

    return "Not installed";
}

void ProjectsPage::ShowReport(HubLauncher::Report report)
{
    Report(std::move(report.Message), report.Failed);
}

// Shown on the page, and logged
void ProjectsPage::Report(std::string message, bool failed)
{
    if (failed)
    {
        TR_WARN("Hub: {}", message);
    }
    else
    {
        TR_INFO("Hub: {}", message);
    }

    m_Message = std::move(message);
    m_MessageFailed = failed;
}