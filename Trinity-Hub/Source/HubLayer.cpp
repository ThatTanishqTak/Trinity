#include "HubLayer.hpp"

#include "HubInstaller.hpp"
#include "HubText.hpp"
#include "HubVersion.hpp"
#include "Pages/InstallsPage.hpp"
#include "Pages/NewProjectPage.hpp"
#include "Pages/ProjectsPage.hpp"
#include "Pages/SettingsPage.hpp"

#include <Trinity/UI/Widgets.hpp>

#include <imgui.h>

#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
    constexpr ImGuiWindowFlags c_ShellFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    // As Unity Hub's default projects location, in Documents, or in Hub's working folder without
    constexpr std::string_view c_ProjectsFolder = "Trinity Projects";
    // Trinity's mark before Hub's name in its title bar, against the size of the text, as in Forge's
    constexpr float c_MarkInFonts = 0.9f;
}

HubLayer::HubLayer(std::filesystem::path directory, std::filesystem::path installsDirectory, std::string releasesUrl) : Layer("Hub"), m_Directory(std::move(directory)), m_DefaultInstallsDirectory(std::move(installsDirectory)), m_DefaultProjectsDirectory(Trinity::Process::GetDocumentsDirectory() / std::filesystem::path(c_ProjectsFolder)), m_Releases(std::move(releasesUrl)), m_Projects(m_Directory / std::filesystem::path(Trinity::ProjectList::c_FileName)), m_Launcher(m_Directory)
{
    std::error_code l_Error;
    m_DefaultProjectsDirectory = std::filesystem::absolute(m_DefaultProjectsDirectory, l_Error);
    Trinity::Scope<InstallsPage> l_Installs = Trinity::CreateScope<InstallsPage>(m_Installs, m_Releases, m_Settings, m_PathPrompt, m_Requirements, m_DefaultInstallsDirectory);
    InstallsPage* l_InstallsPage = l_Installs.get();
    // A version downloaded from a project's offer is installed by the Installs page, which is shown
    Trinity::Scope<ProjectsPage> l_Projects = Trinity::CreateScope<ProjectsPage>(m_Projects, m_Installs, m_Releases, m_Settings, m_PathPrompt, m_Launcher, [this, l_InstallsPage](const HubRelease& release)
    {
        l_InstallsPage->DownloadRelease(release);
        ShowPage(1);
    }, [this]
    {
        m_NewProjectShown = true;
        m_NewProject->OnShow();
    });
    ProjectsPage* l_ProjectsPage = l_Projects.get();
    // A project New Project started is told of on the Projects page, listed once Forge adds it
    m_NewProject = Trinity::CreateScope<NewProjectPage>(m_Installs, m_Settings, m_PathPrompt, m_Launcher, m_Requirements, m_DefaultProjectsDirectory, [this, l_ProjectsPage](HubLauncher::Report report)
    {
        l_ProjectsPage->ShowReport(std::move(report));
        ShowPage(0);
    }, [this] { ShowPage(0); });
    m_Pages.push_back(std::move(l_Projects));
    m_Pages.push_back(std::move(l_Installs));
    m_Pages.push_back(Trinity::CreateScope<SettingsPage>(m_Settings, m_PathPrompt, m_Directory, m_DefaultInstallsDirectory, m_DefaultProjectsDirectory));
}

// New Project is left for any page the sidebar chooses, the Projects page included
void HubLayer::ShowPage(std::size_t page)
{
    m_NewProjectShown = false;
    m_Page = page;
    m_Pages[m_Page]->OnShow();
}

// Only the UI is drawn, so the window gets no scene. A cut-short install is cleared first
void HubLayer::OnAttach()
{
    m_Settings = HubSettings::Load();
    m_Settings.Apply();
    HubInstaller::CleanUp(m_Settings.GetInstallsDirectory(m_DefaultInstallsDirectory));
    m_Installs.Load();
    m_Requirements.Check(m_Installs);
    m_Releases.Load();
    m_Projects.Refresh();

    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    l_Renderer.SetSceneCopy(false);
    l_Renderer.SetTitle(std::format("Trinity Hub {}", HubVersion::c_Version));

    TR_INFO("Hub: Trinity Hub {}, built on Trinity {}", HubVersion::c_Version, Trinity::GetVersionDescription());
    TR_INFO("Hub: keeping its lists and settings in {}, and installing versions in {}", HubText::ToUtf8(m_Directory), HubText::ToUtf8(m_Settings.GetInstallsDirectory(m_DefaultInstallsDirectory)));
}

// A dialog asked for opens here, between frames, and every page takes what its jobs finished
void HubLayer::OnUpdate([[maybe_unused]] Trinity::Timestep timestep)
{
    m_Chrome.Update(Trinity::Application::Get().GetWindow(), m_Settings.CustomTitleBar);
    m_PathPrompt.Update();
    m_Installs.Update();
    m_Requirements.Update(m_Installs);
    m_Releases.Update();
    m_Projects.Update();
    for (const Trinity::Scope<HubPage>& it_Page : m_Pages)
    {
        it_Page->Update();
    }

    m_NewProject->Update();
}

// One window over the whole of the main one, the sidebar down its left and the page beside it
void HubLayer::OnImGuiRender()
{
    const ImGuiViewport* l_Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(l_Viewport->WorkPos);
    ImGui::SetNextWindowSize(l_Viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool l_Open = ImGui::Begin("Trinity Hub###HubShell", nullptr, c_ShellFlags);
    ImGui::PopStyleVar(3);
    if (l_Open)
    {
        if (m_Chrome.IsActive())
        {
            DrawTitleBar();
        }

        DrawSidebar();
        ImGui::SameLine(0.0f, 0.0f);

        const float l_Padding = ImGui::GetFontSize() * 1.5f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(l_Padding, l_Padding));
        if (ImGui::BeginChild("##HubPage", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            (m_NewProjectShown ? *m_NewProject : *m_Pages[m_Page]).Draw();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        m_PathPrompt.Draw();
    }

    ImGui::End();
}

// In the sidebar's colour, with a line under it, and as tall as Forge's. The sidebar and the page begin below it
void HubLayer::DrawTitleBar()
{
    Trinity::Window& l_Window = Trinity::Application::Get().GetWindow();
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const ImVec2 l_Min = ImGui::GetWindowPos();
    const ImVec2 l_Max(l_Min.x + ImGui::GetWindowWidth(), l_Min.y + Trinity::Tools::WindowChrome::GetHeight());
    const float l_Middle = (l_Min.y + l_Max.y) * 0.5f;
    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    l_DrawList.AddRectFilled(l_Min, l_Max, ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    l_DrawList.AddLine(ImVec2(l_Min.x, l_Max.y - 1.0f), ImVec2(l_Max.x, l_Max.y - 1.0f), ImGui::GetColorU32(ImGuiCol_Border));

    // Trinity's mark and Hub's name, which the sidebar leaves to the bar
    const float l_Mark = std::round(ImGui::GetFontSize() * c_MarkInFonts);
    const float l_Left = l_Min.x + l_Style.ItemSpacing.x;
    Trinity::Tools::WindowChrome::DrawMark(l_DrawList, ImVec2(l_Left + l_Mark * 0.5f, l_Middle), l_Mark);
    l_DrawList.AddText(ImVec2(l_Left + l_Mark + l_Style.ItemSpacing.x, std::round(l_Middle - ImGui::GetFontSize() * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text), "Trinity Hub");

    // Minimise, maximise or restore, and close, against the bar's right end as Windows has them
    const ImVec2 l_ButtonsMin(l_Max.x - Trinity::Tools::WindowChrome::GetButtonsWidth(), l_Min.y);
    m_Chrome.DrawButtons(l_Window, l_ButtonsMin, l_Max, [] { Trinity::Application::Get().RequestClose(); });

    // The bar is the only part of the shell its sidebar and its page do not cover, so the mouse over the shell itself with nothing under it is on the bar's empty part
    const ImVec2 l_Mouse = ImGui::GetMousePos();
    m_Chrome.DragBy(l_Mouse.y >= l_Min.y && l_Mouse.y < l_Max.y && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered());

    // The buttons keep all but a sliver of the window's top edge, which elsewhere along the bar resizes it. In the window's own pixels, from its top left
    const ImVec2 l_Origin = ImGui::GetMainViewport()->Pos;
    l_Window.SetTitleBarControls({ { static_cast<std::int32_t>(std::floor(l_ButtonsMin.x - l_Origin.x)), static_cast<std::int32_t>(std::floor(l_ButtonsMin.y - l_Origin.y)), static_cast<std::int32_t>(std::ceil(l_Max.x - l_Origin.x)), static_cast<std::int32_t>(std::ceil(l_Max.y - l_Origin.y)) } });

    ImGui::SetCursorScreenPos(ImVec2(l_Min.x, l_Max.y));
}

// The pages by icon and name, the chosen one lit, with Hub's version at the foot. Hub's name is over them only where the system's title bar is, as Hub's own title bar says it
void HubLayer::DrawSidebar()
{
    const float l_Width = ImGui::GetFontSize() * 13.0f;
    const float l_Padding = ImGui::GetFontSize() * 0.75f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(l_Padding, l_Padding));
    if (ImGui::BeginChild("##HubSidebar", ImVec2(l_Width, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        if (!m_Chrome.IsActive())
        {
            ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), ImGui::GetStyle().FontSizeBase * 1.3f);
            ImGui::TextUnformatted("Trinity Hub");
            ImGui::PopFont();
            ImGui::Spacing();
            ImGui::Spacing();
        }

        const float l_Row = ImGui::GetFrameHeight() * 1.4f;
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));

        // Main navigation: Projects and Installs
        for (std::size_t it_Index = 0; it_Index + 1 < m_Pages.size(); ++it_Index)
        {
            const HubPage& l_Page = *m_Pages[it_Index];
            const std::string l_Label = std::format("{}  {}###HubPage{}", l_Page.GetIcon(), l_Page.GetName(), it_Index);
            const bool l_Picked = ImGui::Selectable(l_Label.c_str(), it_Index == m_Page, ImGuiSelectableFlags_None, ImVec2(0.0f, l_Row));
            if (it_Index == m_Page)
            {
                Trinity::Widgets::DrawSelectionEdge();
            }

            if (l_Picked && (it_Index != m_Page || m_NewProjectShown))
            {
                ShowPage(it_Index);
            }
        }

        // Bottom navigation: Settings
        if (!m_Pages.empty())
        {
            const std::size_t l_Index = m_Pages.size() - 1;
            const HubPage& l_Page = *m_Pages[l_Index];

            const float l_FooterHeight = ImGui::GetTextLineHeight();
            const float l_Spacing = ImGui::GetStyle().ItemSpacing.y;

            const float l_SettingsY = ImGui::GetWindowHeight() - l_Padding - l_FooterHeight - l_Spacing * 2.0f - l_Row;

            if (l_SettingsY > ImGui::GetCursorPosY())
            {
                ImGui::SetCursorPosY(l_SettingsY);
            }

            const std::string l_Label = std::format("{}  {}###HubPage{}", l_Page.GetIcon(), l_Page.GetName(), l_Index);

            const bool l_Picked = ImGui::Selectable(l_Label.c_str(), l_Index == m_Page, ImGuiSelectableFlags_None, ImVec2(0.0f, l_Row));
            if (l_Index == m_Page)
            {
                Trinity::Widgets::DrawSelectionEdge();
            }

            if (l_Picked)
            {
                ShowPage(l_Index);
            }
        }
        ImGui::PopStyleVar();

        const std::string l_Hub = std::format("Trinity Hub {}", HubVersion::c_Version);
        const float l_Foot = ImGui::GetWindowHeight() - l_Padding - ImGui::GetTextLineHeight();
        if (l_Foot > ImGui::GetCursorPosY())
        {
            ImGui::SetCursorPosY(l_Foot);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(l_Hub.c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("%s", std::format("Built on Trinity {}", Trinity::GetVersionDescription()).c_str());
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}