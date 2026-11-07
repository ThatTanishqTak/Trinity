#include "Pages/SettingsPage.hpp"

#include <Trinity.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace
{
    std::string FormatPercent(float scale)
    {
        return std::format("{}%", static_cast<int>(std::lround(scale * 100.0f)));
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }
}

SettingsPage::SettingsPage(HubSettings& settings, std::filesystem::path directory) : m_Settings(settings), m_Directory(std::move(directory))
{

}

const char* SettingsPage::GetIcon() const
{
    return Trinity::Icons::c_Gear;
}

void SettingsPage::Draw()
{
    HubPages::DrawTitle("Settings");
    DrawAppearance();
    DrawFolders();
    DrawAbout();
}

// As Forge's preferences offer them: a theme of ImGui's three, and a size on top of the monitor's own scale, which Hub follows by itself
void SettingsPage::DrawAppearance()
{
    HubPages::DrawSection("Appearance");

    HubPages::DrawLabel("Theme");
    const auto a_Theme = std::ranges::find(HubSettings::c_Themes, std::string_view(m_Settings.ThemeKey), &HubSettings::Theme::Key);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::BeginCombo("##Theme", a_Theme != HubSettings::c_Themes.end() ? a_Theme->Name : m_Settings.ThemeKey.c_str()))
    {
        for (const HubSettings::Theme& it_Theme : HubSettings::c_Themes)
        {
            if (ImGui::Selectable(it_Theme.Name, it_Theme.Key == m_Settings.ThemeKey))
            {
                m_Settings.ThemeKey = it_Theme.Key;
                Change();
            }
        }

        ImGui::EndCombo();
    }

    HubPages::DrawLabel("UI Scale");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::BeginCombo("##Scale", FormatPercent(m_Settings.Scale).c_str()))
    {
        for (const float it_Preset : HubSettings::c_ScalePresets)
        {
            if (ImGui::Selectable(FormatPercent(it_Preset).c_str(), it_Preset == m_Settings.Scale))
            {
                m_Settings.Scale = it_Preset;
                Change();
            }
        }

        ImGui::EndCombo();
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("%s", std::format("On top of the monitor's own scale, which Hub follows by itself, so text is {:.0f} px now", ImGui::GetFontSize()).c_str());
    }
}

void SettingsPage::DrawFolders()
{
    HubPages::DrawSection("Hub's Folder");
    const std::string l_Directory = ToUtf8(m_Directory);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Hub keeps its lists of projects and engine versions, and these settings, in");
    ImGui::PopStyleColor();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(l_Directory.c_str());
    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Open", Trinity::Icons::c_FolderOpen).c_str()))
    {
        if (const Trinity::Expected<void, std::string> l_Opened = Trinity::Process::OpenWithSystem(m_Directory); !l_Opened)
        {
            TR_WARN("Hub: {}", l_Opened.GetError());
        }
    }
}

void SettingsPage::DrawAbout()
{
    HubPages::DrawSection("About");
    const Trinity::RHI::DeviceInfo& l_Device = Trinity::Application::Get().GetDevice().GetInfo();
    HubPages::DrawLabel("Hub");
    ImGui::TextUnformatted(std::format("Trinity Hub, on Trinity {}", Trinity::GetVersionDescription()).c_str());
    HubPages::DrawLabel("Graphics");
    ImGui::TextUnformatted(std::format("{}, {}", Trinity::ToString(l_Device.API), l_Device.AdapterName.empty() ? std::string("no adapter") : l_Device.AdapterName).c_str());
}

// Shown from the next frame on, and kept for the next time Hub starts
void SettingsPage::Change()
{
    m_Settings.Apply();
    m_Settings.Save();
}