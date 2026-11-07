#include "Pages/InstallsPage.hpp"

#include "HubText.hpp"

#include <Trinity.hpp>

#include <imgui.h>

#include <array>
#include <format>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    constexpr const char* c_DeletePopup = "Delete Version###HubDeleteVersion";
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

InstallsPage::InstallsPage(HubInstalls& installs, HubSettings& settings, HubPathPrompt& prompt, std::filesystem::path defaultInstallsDirectory) : m_Installs(installs), m_Settings(settings), m_Prompt(prompt), m_DefaultInstallsDirectory(std::move(defaultInstallsDirectory))
{

}

const char* InstallsPage::GetIcon() const
{
    return Trinity::Icons::c_Cube;
}

// A build tree built again since shows its new commit
void InstallsPage::OnShow()
{
    m_Installs.Refresh();
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

    Report(std::move(l_Result->Message), !l_Result->Succeeded && !l_Result->Cancelled);
}

void InstallsPage::Draw()
{
    HubPages::DrawTitle("Installs");
    DrawToolbar();
    DrawStatus();
    ImGui::Spacing();

    if (m_Installs.GetInstalls().empty())
    {
        HubPages::DrawEmpty("No engine versions yet", "Install a version from its zip, or locate one, such as a build tree's bin. Each is listed here, with the platforms it exports games to.");
    }
    else
    {
        DrawList();
    }

    DrawDeletePopup();
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
        Report(std::format("Nothing was located, as {}", l_Manifest.GetError()), true);

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

    std::vector<std::pair<std::string, std::filesystem::path>> l_Installed;
    for (const HubInstall& it_Install : m_Installs.GetInstalls())
    {
        if (it_Install.Source == HubInstall::Origin::Installed && !it_Install.Missing)
        {
            l_Installed.emplace_back(HubInstalls::GetInstallName(it_Install.Manifest), it_Install.Directory);
        }
    }

    m_Message.clear();
    m_Installer.Install(zip, m_Settings.GetInstallsDirectory(m_DefaultInstallsDirectory), std::move(l_Installed));
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
    if (m_MessageFailed)
    {
        HubPages::DrawWarning(m_Message);
    }
    else
    {
        ImGui::TextWrapped("%s", m_Message.c_str());
    }
}

void InstallsPage::DrawList()
{
    constexpr ImGuiTableFlags c_Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
    if (!ImGui::BeginTable("##Installs", 9, c_Flags))
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
            HubPages::DrawWarning("Unreadable");
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

// Shown on the page, and logged
void InstallsPage::Report(std::string message, bool failed)
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