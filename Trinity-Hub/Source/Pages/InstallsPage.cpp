#include "Pages/InstallsPage.hpp"

#include "HubText.hpp"

#include <Trinity.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    constexpr const char* c_DeletePopup = "Delete Version###HubDeleteVersion";
    constexpr const char* c_NeedsPopup = "What This Version Needs###HubNeeds";
    constexpr std::array<Trinity::FileDialogFilter, 1> c_ZipFilters{ { { "Trinity version", "zip" } } };

    std::string JoinNames(const std::vector<std::string>& names)
    {
        std::string l_Text;
        for (const std::string& it_Name : names)
        {
            l_Text += l_Text.empty() ? it_Name : ", " + it_Name;
        }

        return l_Text.empty() ? std::string("none") : l_Text;
    }

    std::string DescribeCommit(const Trinity::EngineManifest& manifest)
    {
        if (manifest.Commit.empty())
        {
            return "none";
        }

        return std::format("{}{}", std::string_view(manifest.Commit).substr(0, Trinity::c_ShortCommitLength), manifest.Modified ? ", modified" : "");
    }
}

InstallsPage::InstallsPage(HubInstalls& installs, HubReleases& releases, HubSettings& settings, HubPathPrompt& prompt, HubRequirements& requirements, std::filesystem::path defaultInstallsDirectory) : m_Installs(installs), m_Releases(releases), m_Settings(settings), m_Prompt(prompt), m_Requirements(requirements), m_DefaultInstallsDirectory(std::move(defaultInstallsDirectory))
{

}

const char* InstallsPage::GetIcon() const
{
    return Trinity::Icons::c_Cube;
}

// A build tree built again since shows its new commit, and GitHub is asked for its releases, unless it was within the last minute
void InstallsPage::OnShow()
{
    m_Installs.Refresh();
    m_Releases.Refresh();
}

void InstallsPage::Update()
{
    std::optional<HubInstaller::Result> l_Result = m_Installer.Update();
    if (!l_Result)
    {
        return;
    }

    if (l_Result->Succeeded && l_Result->Deleted)
    {
        m_Installs.Remove(l_Result->Directory);
    }
    else if (l_Result->Succeeded)
    {
        m_Installs.Add(l_Result->Directory, HubInstall::Origin::Installed, std::move(l_Result->Manifest));
    }

    Report(std::move(l_Result->Message), !l_Result->Succeeded && !l_Result->Cancelled, l_Result->NeedsNewerHub);
}

void InstallsPage::Draw()
{
    HubPages::DrawTitle("Installs");
    DrawNewerNotice();
    DrawStatus();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##InstallsTabs"))
    {
        if (ImGui::BeginTabItem("Installed"))
        {
            ImGui::Spacing();
            DrawToolbar();
            ImGui::Spacing();
            if (m_Installs.GetInstalls().empty())
            {
                HubPages::DrawEmpty("No engine versions yet", "Install a version from its zip, or locate one, such as a build tree's bin. Each is listed here, with the platforms it exports games to.");
            }
            else
            {
                DrawList();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Releases"))
        {
            ImGui::Spacing();
            DrawReleases();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    DrawDeletePopup();
    DrawNeedsPopup();
}

// The folder's manifest is read as it is chosen, and must be of a version this machine runs
void InstallsPage::Locate(const std::filesystem::path& directory)
{
    const std::string l_Directory = HubText::ToUtf8(directory);
    std::error_code l_Error;
    if (!std::filesystem::is_directory(directory, l_Error))
    {
        Report(std::format("Nothing was located, as {} is not a folder", l_Directory), true);

        return;
    }

    Trinity::Expected<Trinity::EngineManifest, std::string> l_Manifest = Trinity::ReadEngineManifest(directory);
    if (!l_Manifest)
    {
        Report(std::format("Nothing was located, as {}", l_Manifest.GetError()), true, HubText::ReadFormat(directory / HubText::FromUtf8(Trinity::EngineManifest::c_FileName)).value_or(0) > Trinity::EngineManifest::c_FormatVersion);

        return;
    }

    if (l_Manifest->Platform != Trinity::GetBuildPlatform() || l_Manifest->Architecture != Trinity::GetBuildArchitecture())
    {
        Report(std::format("{} was not located, as it holds {} for {} on {}, and this machine runs {} on {}", l_Directory, HubInstalls::Describe(*l_Manifest), l_Manifest->Platform, l_Manifest->Architecture, Trinity::GetBuildPlatform(), Trinity::GetBuildArchitecture()), true);

        return;
    }

    if (const HubInstall* l_Listed = m_Installs.Find(directory))
    {
        Report(std::format("{} in {} is listed already", HubInstalls::Describe(l_Listed->Manifest), HubText::ToUtf8(l_Listed->Directory)), false);

        return;
    }

    const std::string l_Description = HubInstalls::Describe(*l_Manifest);
    m_Installs.Add(directory, HubInstall::Origin::Located, std::move(*l_Manifest));
    Report(std::format("Located {} in {}", l_Description, l_Directory), false);
}

// Into the installs folder, unless the version is installed somewhere already, which it then replaces where it is, once asked
void InstallsPage::InstallFromFile(const std::filesystem::path& zip)
{
    if (m_Installer.IsBusy())
    {
        Report(std::format("{} was not installed, as Hub is installing or deleting another version, one at a time", HubText::ToUtf8(zip.filename())), true);

        return;
    }

    m_Message.clear();
    m_Installer.Install(zip, m_Settings.GetInstallsDirectory(m_DefaultInstallsDirectory), GetInstalledFolders());
}

// Downloaded, checked against its hash, then installed as a zip from the disk is, so one already installed is asked about first
void InstallsPage::DownloadRelease(const HubRelease& release)
{
    if (m_Installer.IsBusy())
    {
        Report(std::format("{} was not downloaded, as Hub is installing or deleting another version, one at a time", release.ZipName), true);

        return;
    }

    if (!release.CanCheck())
    {
        Report(std::format("{} was not downloaded, as GitHub gives neither a .sha256 nor a digest for it, so Hub could not check it", release.ZipName), true);

        return;
    }

    m_Message.clear();
    m_Installer.Download(release, m_Settings.GetInstallsDirectory(m_DefaultInstallsDirectory), GetInstalledFolders());
}

// A located version, or one whose folder is gone, leaves the list at once. An installed one's files are deleted, once asked
void InstallsPage::Remove(const std::filesystem::path& directory)
{
    const HubInstall* l_Install = m_Installs.Find(directory);
    if (l_Install == nullptr)
    {
        return;
    }

    if (l_Install->Source == HubInstall::Origin::Located || l_Install->Missing)
    {
        const std::string l_Message = l_Install->Missing ? std::format("Took {} off the list, whose folder {} is gone", HubInstalls::Describe(l_Install->Manifest), HubText::ToUtf8(l_Install->Directory)) : std::format("Took {} in {} off the list, and left its files as they are", HubInstalls::Describe(l_Install->Manifest), HubText::ToUtf8(l_Install->Directory));
        m_Installs.Remove(directory);
        Report(l_Message, false);

        return;
    }

    m_ConfirmDelete = l_Install->Directory;
    m_OpenDeletePopup = true;
}

void InstallsPage::DrawToolbar()
{
    const bool l_Busy = m_Installer.IsBusy();
    ImGui::BeginDisabled(l_Busy);
    if (ImGui::Button(std::format("{} Locate", Trinity::Icons::c_FolderOpen).c_str()))
    {
        m_Prompt.Ask({ HubPathPrompt::Kind::Folder, "Locate a Trinity version: a folder holding Trinity.trversion, such as a build tree's bin",{},{}, [this](const std::filesystem::path& path) { Locate(path); } });
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Lists a version Hub did not install, such as a build tree's bin, whose files Hub never changes");
    }

    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Install from File", Trinity::Icons::c_Download).c_str()))
    {
        m_Prompt.Ask({ HubPathPrompt::Kind::OpenFile, "Install a Trinity version from its zip", c_ZipFilters,{}, [this](const std::filesystem::path& path) { InstallFromFile(path); } });
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", std::format("Unpacks a Trinity-<version>-<platform>-<architecture>.zip into {}, checked first against the .sha256 beside it", HubText::ToUtf8(m_Settings.GetInstallsDirectory(m_DefaultInstallsDirectory))).c_str());
    }

    ImGui::EndDisabled();
}

// What is being done with its progress and Cancel, the question an install over an installed version asks, or what the last thing done came to
void InstallsPage::DrawStatus()
{
    const HubInstaller::Stage l_Stage = m_Installer.GetStage();
    if (l_Stage == HubInstaller::Stage::Asking)
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s %s", Trinity::Icons::c_InfoCircle, m_Installer.Describe().c_str());
        if (ImGui::Button("Replace"))
        {
            m_Installer.Replace();
        }

        ImGui::SameLine();
        if (ImGui::Button("Keep Installed"))
        {
            m_Installer.KeepInstalled();
        }

        return;
    }

    if (l_Stage != HubInstaller::Stage::Idle)
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_Installer.Describe().c_str());
        const float l_Progress = m_Installer.GetProgress();
        const float l_Width = l_Stage == HubInstaller::Stage::Deleting ? 0.0f : -ImGui::CalcTextSize("Cancel").x - ImGui::GetStyle().FramePadding.x * 2.0f - ImGui::GetStyle().ItemSpacing.x;
        ImGui::ProgressBar(l_Progress >= 0.0f ? l_Progress : -1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(l_Width, 0.0f), l_Progress >= 0.0f ? std::format("{:.0f}%", l_Progress * 100.0f).c_str() : "");
        if (l_Stage != HubInstaller::Stage::Deleting)
        {
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                m_Installer.Cancel();
            }
        }

        return;
    }

    if (m_Message.empty())
    {
        return;
    }

    ImGui::Spacing();
    if (ImGui::SmallButton(std::format("{}##Dismiss", Trinity::Icons::c_TimesCircle).c_str()))
    {
        m_Message.clear();

        return;
    }

    ImGui::SameLine();
    if (m_MessageNeedsNewerHub)
    {
        HubPages::DrawNeedsNewerHub(m_Message);
    }
    else if (m_MessageFailed)
    {
        HubPages::DrawWarning(m_Message);
    }
    else
    {
        ImGui::TextWrapped("%s", m_Message.c_str());
    }
}

// Installs.yaml a newer Hub wrote, which lists nothing here, or versions whose manifests a newer Trinity wrote
void InstallsPage::DrawNewerNotice()
{
    if (m_Installs.IsFromNewerHub())
    {
        HubPages::DrawNeedsNewerHub("Installs.yaml was written by a newer Hub, in a format this one does not read, so no version is listed from it, and it is left as it is, keeping none located or installed here");
        ImGui::Spacing();

        return;
    }

    const std::size_t l_Newer = static_cast<std::size_t>(std::ranges::count_if(m_Installs.GetInstalls(), &HubInstall::NeedsNewerHub));
    if (l_Newer == 0)
    {
        return;
    }

    HubPages::DrawNeedsNewerHub(l_Newer == 1 ? std::string("A version listed here has a manifest a newer Trinity wrote, in a format this Hub does not read, so it cannot be used") : std::format("{} versions listed here have manifests a newer Trinity wrote, in a format this Hub does not read, so they cannot be used", l_Newer));
    ImGui::Spacing();
}

void InstallsPage::DrawList()
{
    constexpr ImGuiTableFlags c_Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
    if (!ImGui::BeginTable("##Installs", 10, c_Flags))
    {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Version");
    ImGui::TableSetupColumn("State");
    ImGui::TableSetupColumn("Commit");
    ImGui::TableSetupColumn("Date");
    ImGui::TableSetupColumn("Configurations");
    ImGui::TableSetupColumn("Exports");
    ImGui::TableSetupColumn("Needs");
    ImGui::TableSetupColumn("Size");
    ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##Actions");
    ImGui::TableHeadersRow();

    const bool l_Busy = m_Installer.IsBusy();
    std::optional<std::filesystem::path> l_Remove;
    const std::vector<HubInstall>& l_Installs = m_Installs.GetInstalls();
    for (std::size_t it_Index = 0; it_Index < l_Installs.size(); ++it_Index)
    {
        const HubInstall& l_Install = l_Installs[it_Index];
        const Trinity::EngineManifest& l_Manifest = l_Install.Manifest;
        ImGui::PushID(static_cast<int>(it_Index));
        ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeightWithSpacing());

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
        ImGui::TextUnformatted(l_Manifest.Version.empty() ? "unknown" : l_Manifest.Version.c_str());
        ImGui::PopFont();

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (l_Install.Missing)
        {
            HubPages::DrawWarning("Missing");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                ImGui::SetTooltip("Its folder is gone. Remove takes it off the list");
            }
        }
        else if (!l_Install.Problem.empty())
        {
            HubPages::DrawWarning(l_Install.NeedsNewerHub ? "Needs a newer Hub" : "Unreadable");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                ImGui::SetTooltip("%s", l_Install.Problem.c_str());
            }
        }
        else
        {
            ImGui::TextDisabled("%s", l_Install.Source == HubInstall::Origin::Installed ? "Installed" : "Located");
        }

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(DescribeCommit(l_Manifest).c_str());

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(HubText::FormatDate(l_Manifest.Date).c_str());

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(JoinNames(l_Manifest.Configurations).c_str());

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(JoinNames(l_Manifest.Exports).c_str());

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        DrawNeeds(l_Install);

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (l_Install.Missing)
        {
            ImGui::TextDisabled("-");
        }
        else if (l_Install.Size)
        {
            ImGui::TextUnformatted(Trinity::Memory::FormatBytes(*l_Install.Size).c_str());
        }
        else
        {
            ImGui::TextDisabled("counting");
        }

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        HubPages::DrawClipped(HubText::ToUtf8(l_Install.Directory));

        ImGui::TableNextColumn();
        ImGui::BeginDisabled(l_Install.Missing);
        if (ImGui::Button(Trinity::Icons::c_FolderOpen))
        {
            if (const Trinity::Expected<void, std::string> l_Opened = Trinity::Process::OpenWithSystem(l_Install.Directory); !l_Opened)
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
        ImGui::BeginDisabled(l_Busy);
        if (ImGui::Button(Trinity::Icons::c_Trash))
        {
            l_Remove = l_Install.Directory;
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", l_Install.Source == HubInstall::Origin::Installed && !l_Install.Missing ? "Remove, deleting its files once asked" : "Remove from the list, leaving its files as they are");
        }

        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::EndTable();

    // Once the table is drawn, as it changes the list
    if (l_Remove)
    {
        Remove(*l_Remove);
    }
}

// Ready, or what is missing in the colour of a warning, either opening the popup, and nothing for a version this machine cannot run
void InstallsPage::DrawNeeds(const HubInstall& install)
{
    if (!HubInstalls::IsUsable(install))
    {
        ImGui::TextDisabled("-");

        return;
    }

    const std::optional<HubRequirements::Report> l_Report = m_Requirements.GetReport(install);
    if (!l_Report)
    {
        ImGui::TextDisabled("%s", std::format("{} Checking", Trinity::Icons::c_Spinner).c_str());

        return;
    }

    std::vector<std::string> l_Missing;
    for (const HubRequirements::Row& it_Row : l_Report->Rows)
    {
        if (!it_Row.Found)
        {
            l_Missing.push_back(it_Row.Name);
        }
    }

    const bool l_Ready = l_Missing.empty();
    ImGui::PushStyleColor(ImGuiCol_Text, l_Ready ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram));
    const std::string l_Label = l_Ready ? std::format("{} Ready", Trinity::Icons::c_CheckCircle) : std::format("{} {}", Trinity::Icons::c_Warning, JoinNames(l_Missing));
    if (ImGui::Selectable(std::format("{}##Needs", l_Label).c_str(), false, ImGuiSelectableFlags_None, ImVec2(ImGui::CalcTextSize(l_Label.c_str()).x, 0.0f)))
    {
        m_Needs = install.Directory;
        m_OpenNeedsPopup = true;
    }

    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        std::string l_Tooltip = l_Ready ? std::string("This machine has every tool this version's projects build with") : std::string("This machine is missing what this version's projects need");
        for (const std::string* it_Refusal : { &l_Report->CSharpRefusal, &l_Report->CppRefusal })
        {
            if (!it_Refusal->empty())
            {
                l_Tooltip += std::format("\n{}", *it_Refusal);
            }
        }

        ImGui::SetTooltip("%s", std::format("{}\nClick for each tool, where it was found or where to get it", l_Tooltip).c_str());
    }
}

// Closes by itself once the version is off the list, and shows the last look while another is running
void InstallsPage::DrawNeedsPopup()
{
    if (std::exchange(m_OpenNeedsPopup, false))
    {
        ImGui::OpenPopup(c_NeedsPopup);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 40.0f, 0.0f), ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.9f, ImGui::GetMainViewport()->WorkSize.y * 0.9f));
    if (!ImGui::BeginPopupModal(c_NeedsPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    const HubInstall* l_Install = m_Needs ? m_Installs.Find(*m_Needs) : nullptr;
    if (l_Install == nullptr || !HubInstalls::IsUsable(*l_Install))
    {
        m_Needs.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();

        return;
    }

    const float l_Wrap = ImGui::GetFontSize() * 52.0f;
    ImGui::PushTextWrapPos(l_Wrap);
    ImGui::TextUnformatted(std::format("What {} needs on this machine, found as its Forge finds it", HubInstalls::Describe(l_Install->Manifest)).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const std::optional<HubRequirements::Report> l_Report = m_Requirements.GetReport(*l_Install);
    if (!l_Report)
    {
        ImGui::TextDisabled("%s", std::format("{} Looking for the tools, which takes a few seconds", Trinity::Icons::c_Spinner).c_str());
    }
    else if (ImGui::BeginTable("##NeedsRows", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX, ImVec2(l_Wrap, 0.0f)))
    {
        ImGui::TableSetupColumn("Tool");
        ImGui::TableSetupColumn("Needs");
        ImGui::TableSetupColumn("On this machine", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const HubRequirements::Row& it_Row : l_Report->Rows)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
            ImGui::TextUnformatted(it_Row.Name.c_str());
            ImGui::PopFont();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                ImGui::SetTooltip("%s", it_Row.Purpose.c_str());
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(it_Row.Wanted.c_str());

            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0.0f);
            if (it_Row.Found)
            {
                ImGui::TextWrapped("%s", std::format("{} {}", Trinity::Icons::c_CheckCircle, it_Row.Detail).c_str());
                if (!it_Row.Note.empty())
                {
                    ImGui::TextDisabled("%s", it_Row.Note.c_str());
                }
            }
            else
            {
                HubPages::DrawWarning(std::format("{} {}", Trinity::Icons::c_TimesCircle, it_Row.Detail));
                // What it stops, or what it is for where it stops neither language, then where to get one
                const std::string l_Lead = it_Row.StopsCSharp ? std::string("Without it C# scripts do not build") : it_Row.StopsCpp ? std::string("Without it C++ modules do not build") : it_Row.Purpose;
                ImGui::TextDisabled("%s", std::format("{}. {}", l_Lead, it_Row.Remedy).c_str());
            }

            ImGui::PopTextWrapPos();
        }

        ImGui::EndTable();

        ImGui::Spacing();
        ImGui::PushTextWrapPos(l_Wrap);
        for (const Trinity::ScriptLanguage it_Language : { Trinity::ScriptLanguage::CSharp, Trinity::ScriptLanguage::Cpp })
        {
            const std::string& l_Refusal = l_Report->GetRefusal(it_Language);
            const std::string_view l_Name = it_Language == Trinity::ScriptLanguage::CSharp ? "C#" : "C++";
            if (!l_Refusal.empty())
            {
                HubPages::DrawWarning(std::format("{} projects: New Project cannot make them with this version here. {}", l_Name, l_Refusal));
            }
            else
            {
                const bool l_Note = it_Language == Trinity::ScriptLanguage::CSharp && !l_Report->CSharpExportNote.empty();
                ImGui::TextUnformatted(std::format("{} projects: ready{}", l_Name, l_Note ? std::format(". {}", l_Report->CSharpExportNote) : std::string()).c_str());
            }
        }

        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(m_Requirements.IsChecking());
    if (ImGui::Button(m_Requirements.IsChecking() ? std::format("{} Checking", Trinity::Icons::c_Spinner).c_str() : std::format("{} Check Again", Trinity::Icons::c_Refresh).c_str()))
    {
        m_Requirements.Check(m_Installs);
    }

    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Looks for every tool again, as after installing one");
    }

    ImGui::SameLine();
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        m_Needs.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void InstallsPage::DrawDeletePopup()
{
    if (std::exchange(m_OpenDeletePopup, false))
    {
        ImGui::OpenPopup(c_DeletePopup);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(c_DeletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    const HubInstall* l_Install = m_ConfirmDelete ? m_Installs.Find(*m_ConfirmDelete) : nullptr;
    if (l_Install == nullptr || m_Installer.IsBusy())
    {
        m_ConfirmDelete.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();

        return;
    }

    ImGui::TextUnformatted(std::format("Delete {}?", HubInstalls::Describe(l_Install->Manifest)).c_str());
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextDisabled("%s", std::format("Its files in {}{} are deleted. Hub can install it again from its zip.", HubText::ToUtf8(l_Install->Directory), l_Install->Size ? std::format(", {},", Trinity::Memory::FormatBytes(*l_Install->Size)) : std::string()).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    if (ImGui::Button("Delete"))
    {
        m_Installer.Delete(l_Install->Directory, l_Install->Manifest);
        m_ConfirmDelete.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        m_ConfirmDelete.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// GitHub's releases with a zip for this platform, newest first, pre-releases only when asked for. Refresh asks again, at most once a minute, and a list GitHub could not give says why, with the last one kept
void InstallsPage::DrawReleases()
{
    const bool l_Fetching = m_Releases.IsFetching();
    const std::chrono::seconds l_Wait = m_Releases.GetWait();
    ImGui::BeginDisabled(l_Fetching || l_Wait.count() > 0);
    const std::string l_Refresh = l_Wait.count() > 0 && !l_Fetching ? std::format("{} Refresh in {} s###Refresh", Trinity::Icons::c_Refresh, l_Wait.count()) : std::format("{} Refresh###Refresh", Trinity::Icons::c_Refresh);
    if (ImGui::Button(l_Refresh.c_str()))
    {
        m_Releases.Refresh();
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("GitHub answers 60 requests an hour without a token, so Hub asks it at most once a minute");
    }

    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Show pre-releases", &m_ShowPrereleases);

    const std::optional<std::chrono::sys_seconds> l_Fetched = HubText::ParseTime(m_Releases.GetFetchedAt());
    if (l_Fetching)
    {
        ImGui::TextDisabled("Asking GitHub for Trinity's releases...");
    }
    else if (!m_Releases.GetNote().empty())
    {
        HubPages::DrawWarning(l_Fetched ? std::format("{}. This is the list from {}", m_Releases.GetNote(), HubText::DescribeTime(*l_Fetched)) : m_Releases.GetNote());
    }
    else if (l_Fetched)
    {
        ImGui::TextDisabled("%s", std::format("The releases of {} on GitHub, as of {}", HubReleases::c_Repository, HubText::DescribeTime(*l_Fetched)).c_str());
    }

    ImGui::Spacing();

    std::vector<const HubRelease*> l_Shown;
    std::size_t l_Hidden = 0;
    for (const HubRelease& it_Release : m_Releases.GetReleases())
    {
        if (it_Release.Prerelease && !m_ShowPrereleases)
        {
            ++l_Hidden;

            continue;
        }

        l_Shown.push_back(&it_Release);
    }

    std::string l_Footer;
    if (l_Hidden > 0)
    {
        l_Footer = std::format("{} pre-release{} hidden", l_Hidden, l_Hidden == 1 ? "" : "s");
    }

    if (m_Releases.GetOtherReleases() > 0)
    {
        l_Footer += std::format("{}{} release{} with no zip for {} on {}", l_Footer.empty() ? "" : ", and ", m_Releases.GetOtherReleases(), m_Releases.GetOtherReleases() == 1 ? "" : "s", Trinity::GetBuildPlatform(), Trinity::GetBuildArchitecture());
    }

    if (!l_Shown.empty())
    {
        DrawReleaseList(l_Shown);
    }
    else if (l_Fetched)
    {
        HubPages::DrawEmpty(std::format("No releases for {} on {} yet", Trinity::GetBuildPlatform(), Trinity::GetBuildArchitecture()), std::format("Each release of {} on GitHub with a Trinity-<version>-{}-{}.zip is listed here, to download and install.{}", HubReleases::c_Repository, Trinity::GetBuildPlatform(), Trinity::GetBuildArchitecture(), l_Footer.empty() ? std::string() : std::format(" GitHub has {}.", l_Footer)));

        return;
    }

    if (!l_Footer.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", l_Footer.c_str());
    }
}

void InstallsPage::DrawReleaseList(const std::vector<const HubRelease*>& releases)
{
    constexpr ImGuiTableFlags c_Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
    const float l_Footer = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
    if (!ImGui::BeginTable("##Releases", 6, c_Flags, ImVec2(0.0f, std::max(ImGui::GetContentRegionAvail().y - l_Footer, ImGui::GetFrameHeightWithSpacing() * 3.0f))))
    {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Version");
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Published");
    ImGui::TableSetupColumn("Size");
    ImGui::TableSetupColumn("State");
    ImGui::TableSetupColumn("##Actions");
    ImGui::TableHeadersRow();

    const bool l_Busy = m_Installer.IsBusy();
    std::optional<HubRelease> l_Download;
    for (std::size_t it_Index = 0; it_Index < releases.size(); ++it_Index)
    {
        const HubRelease& l_Release = *releases[it_Index];
        ImGui::PushID(static_cast<int>(it_Index));
        ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeightWithSpacing());

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
        ImGui::TextUnformatted(l_Release.Version.c_str());
        ImGui::PopFont();

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        HubPages::DrawClipped(l_Release.Name);

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(HubText::FormatDate(l_Release.PublishedAt).c_str());

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(l_Release.ZipSize > 0 ? Trinity::Memory::FormatBytes(l_Release.ZipSize).c_str() : "-");

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        const bool l_Installed = IsInstalled(l_Release);
        const std::string l_State = std::format("{}{}{}", l_Installed ? "Installed" : "", l_Installed && l_Release.Prerelease ? ", " : "", l_Release.Prerelease ? "Pre-release" : "");
        ImGui::TextDisabled("%s", l_State.c_str());

        ImGui::TableNextColumn();
        ImGui::BeginDisabled(l_Busy || !l_Release.CanCheck());
        if (ImGui::Button(std::format("{} Download", Trinity::Icons::c_Download).c_str()))
        {
            l_Download = l_Release;
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", l_Release.CanCheck() ? std::format("Downloads {}, {}, checks it against {}, and installs it", l_Release.ZipName, Trinity::Memory::FormatBytes(l_Release.ZipSize), l_Release.Sha256Url.empty() ? "GitHub's digest of it" : "the .sha256 beside it").c_str() : "GitHub gives neither a .sha256 nor a digest for its zip, so Hub could not check it");
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(l_Release.Page.empty());
        if (ImGui::Button(Trinity::Icons::c_Globe))
        {
            if (const Trinity::Expected<void, std::string> l_Opened = Trinity::Process::OpenWithSystem(std::filesystem::path(l_Release.Page)); !l_Opened)
            {
                Report(l_Opened.GetError(), true);
            }
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("Its release notes on GitHub");
        }

        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::EndTable();

    // Once the table is drawn, as the release is copied before the list could change
    if (l_Download)
    {
        DownloadRelease(*l_Download);
    }
}

bool InstallsPage::IsInstalled(const HubRelease& release) const
{
    return std::ranges::any_of(m_Installs.GetInstalls(), [&release](const HubInstall& install)
    {
        return !install.Missing && install.Manifest.Version == release.Version && install.Manifest.Platform == Trinity::GetBuildPlatform() && install.Manifest.Architecture == Trinity::GetBuildArchitecture();
    });
}

std::vector<std::pair<std::string, std::filesystem::path>> InstallsPage::GetInstalledFolders() const
{
    std::vector<std::pair<std::string, std::filesystem::path>> l_Installed;
    for (const HubInstall& it_Install : m_Installs.GetInstalls())
    {
        if (it_Install.Source == HubInstall::Origin::Installed && !it_Install.Missing)
        {
            l_Installed.emplace_back(HubInstalls::GetInstallName(it_Install.Manifest), it_Install.Directory);
        }
    }

    return l_Installed;
}

// Shown on the page, and logged
void InstallsPage::Report(std::string message, bool failed, bool needsNewerHub)
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
    m_MessageNeedsNewerHub = needsNewerHub;
}