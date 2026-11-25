#include "StartScreen.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr float c_WidthInFonts = 40.0f;
    // Forge's name against the size of the text under it
    constexpr float c_NameScale = 1.5f;

    // What the last row offers, each by the command it runs
    struct Link
    {
        const char* Label;
        std::string_view Command;
    };

    constexpr std::array<Link, 4> c_Links{ { { "Documentation", "help.documentation" },{ "Keyboard Shortcuts", "help.shortcuts" },{ "Release Notes", "help.releaseNotes" },{ "Report a Bug", "help.reportBug" } } };

    std::string WithIcon(const char* icon, std::string_view text)
    {
        return std::format("{} {}", icon, text);
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }
}

// The pictures are read again each time the screen comes back, as a project just closed may have a new one
void StartScreen::Update()
{
    if (const bool l_Shown = !m_Session.HasProject(); l_Shown != std::exchange(m_Shown, l_Shown))
    {
        if (l_Shown)
        {
            m_Thumbnails.Recheck();
        }
        else
        {
            m_Thumbnails.Clear();
        }
    }

    m_Thumbnails.Update();
}

void StartScreen::Draw()
{
    if (m_Session.HasProject())
    {
        return;
    }

    const ImGuiViewport* l_Viewport = ImGui::GetMainViewport();
    const float l_Width = ImGui::GetFontSize() * c_WidthInFonts;
    ImGui::SetNextWindowPos(l_Viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(l_Width, 0.0f), ImVec2(l_Width, FLT_MAX));
    ImGui::SetNextWindowViewport(l_Viewport->ID);
    const ImGuiWindowFlags l_Flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("###StartScreen", nullptr, l_Flags))
    {
        ImGui::End();

        return;
    }

    DrawHeader();
    ImGui::Spacing();

    const ImGuiStyle& l_Style = ImGui::GetStyle();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Projects");
    const float l_NewWidth = ImGui::CalcTextSize(WithIcon(Trinity::Icons::c_File, "New Project...").c_str()).x + l_Style.FramePadding.x * 2.0f;
    const float l_OpenWidth = ImGui::CalcTextSize(WithIcon(Trinity::Icons::c_FolderOpen, "Open Project...").c_str()).x + l_Style.FramePadding.x * 2.0f;
    ImGui::SameLine();
    const float l_RowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(std::max(l_RowRight - l_NewWidth - l_OpenWidth - l_Style.ItemSpacing.x, ImGui::GetCursorPosX()));
    if (ImGui::Button(WithIcon(Trinity::Icons::c_File, "New Project...").c_str()))
    {
        m_Session.Request(EditorSession::Command::NewProject);
    }

    ImGui::SameLine();
    if (ImGui::Button(WithIcon(Trinity::Icons::c_FolderOpen, "Open Project...").c_str()))
    {
        m_Session.Request(EditorSession::Command::OpenProject);
    }

    ImGui::Separator();
    const std::size_t l_Count = m_Session.GetRecentProjects().size();
    if (l_Count == 0)
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("No project opened yet. New Project makes one in an empty folder, and Open Project opens a .trproj file, or the folder holding one");
        ImGui::PopTextWrapPos();
    }

    std::optional<std::filesystem::path> l_Removed;
    for (std::size_t it_Index = 0; it_Index < l_Count; ++it_Index)
    {
        DrawProject(it_Index, l_Removed);
    }

    ImGui::Separator();
    DrawLinks();

    // Once the rows are drawn, as it changes the list they are drawn from
    if (l_Removed)
    {
        m_Session.RemoveRecentProject(*l_Removed);
    }

    ImGui::End();
}

// The name large and bold, and the version small at the row's end, the two standing on one line
void StartScreen::DrawHeader() const
{
    const std::string l_Version = std::format("Version {}", Trinity::GetVersionString());
    const float l_Right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float l_Top = ImGui::GetCursorPosY();
    ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), ImGui::GetStyle().FontSizeBase * c_NameScale);
    ImGui::TextUnformatted("Trinity Forge");
    const float l_NameHeight = ImGui::GetTextLineHeight();
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(std::max(l_Right - ImGui::CalcTextSize(l_Version.c_str()).x, ImGui::GetCursorPosX()), l_Top + std::floor((l_NameHeight - ImGui::GetTextLineHeight()) * 0.8f)));
    ImGui::TextDisabled("%s", l_Version.c_str());
}

void StartScreen::DrawProject(std::size_t index, std::optional<std::filesystem::path>& removed)
{
    const EditorSession::RecentProject& l_Project = m_Session.GetRecentProjects()[index];
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const float l_Line = ImGui::GetTextLineHeight();
    const float l_Height = l_Line * 2.0f + l_Style.ItemSpacing.y + l_Style.FramePadding.y * 2.0f;
    const float l_PictureWidth = std::round(l_Height * 16.0f / 9.0f);

    ImGui::PushID(static_cast<int>(index));
    const ImVec2 l_Min = ImGui::GetCursorScreenPos();
    // The row's own edge, as the selectable's rectangle reaches half the item spacing past it
    const float l_Right = l_Min.x + ImGui::GetContentRegionAvail().x;
    if (ImGui::Selectable("##Project", false, ImGuiSelectableFlags_None, ImVec2(0.0f, l_Height)))
    {
        if (l_Project.Missing)
        {
            ImGui::OpenPopup("##ProjectMenu");
        }
        else
        {
            m_Session.RequestOpenProject(l_Project.Path);
        }
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("%s", (l_Project.Missing ? std::format("{} is gone, moved or deleted since it was opened. Click to take it off the list", ToUtf8(l_Project.Path)) : ToUtf8(l_Project.Path)).c_str());
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("##ProjectMenu");
    }

    if (ImGui::BeginPopup("##ProjectMenu"))
    {
        if (ImGui::MenuItem(std::format("{} Remove from List", Trinity::Icons::c_TimesCircle).c_str()))
        {
            removed = l_Project.Path;
        }

        ImGui::EndPopup();
    }

    // A project that is gone has no picture to read, and one never saved in this version has none yet
    const ImTextureData* l_Picture = l_Project.Missing ? nullptr : m_Thumbnails.Get(Trinity::Project::GetThumbnailFile(l_Project.Path.parent_path()));
    Trinity::Tools::Thumbnails::Draw(l_Picture, l_Project.Missing ? Trinity::Icons::c_Warning : Trinity::Icons::c_Cube, l_Min, ImVec2(l_Min.x + l_PictureWidth, l_Min.y + l_Height));

    // The name over its folder, with when it was opened, or that it is missing, at the row's end
    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    const ImU32 l_DimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const std::string l_When = l_Project.Missing ? std::string("Missing") : EditorSession::FormatAge(l_Project.OpenedAt);
    const float l_WhenLeft = l_Right - ImGui::CalcTextSize(l_When.c_str()).x;
    const ImVec2 l_NameAt(l_Min.x + l_PictureWidth + l_Style.ItemSpacing.x, l_Min.y + l_Style.FramePadding.y);
    l_DrawList.AddText(ImVec2(l_WhenLeft, l_NameAt.y), l_Project.Missing ? Trinity::Theme::GetColorU32(Trinity::ThemeColor::Warning) : l_DimColor, l_When.c_str());
    l_DrawList.PushClipRect(l_NameAt, ImVec2(l_WhenLeft - l_Style.ItemSpacing.x, l_Min.y + l_Height), true);
    l_DrawList.AddText(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), ImGui::GetFontSize(), l_NameAt, ImGui::GetColorU32(l_Project.Missing ? ImGuiCol_TextDisabled : ImGuiCol_Text), l_Project.Name.c_str());
    l_DrawList.PopClipRect();
    l_DrawList.PushClipRect(l_NameAt, ImVec2(l_Right, l_Min.y + l_Height), true);
    l_DrawList.AddText(ImVec2(l_NameAt.x, l_NameAt.y + l_Line + l_Style.ItemSpacing.y), l_DimColor, ToUtf8(l_Project.Path.parent_path()).c_str());
    l_DrawList.PopClipRect();
    ImGui::PopID();
}

// Spread over the row's width, the first at its left edge and the last at its right
void StartScreen::DrawLinks() const
{
    float l_Used = 0.0f;
    for (const Link& it_Link : c_Links)
    {
        l_Used += ImGui::CalcTextSize(it_Link.Label).x;
    }

    const float l_Gap = std::max((ImGui::GetContentRegionAvail().x - l_Used) / static_cast<float>(c_Links.size() - 1), ImGui::GetStyle().ItemSpacing.x);
    for (std::size_t it_Index = 0; it_Index < c_Links.size(); ++it_Index)
    {
        if (it_Index != 0)
        {
            ImGui::SameLine(0.0f, l_Gap);
        }

        if (ImGui::TextLink(c_Links[it_Index].Label))
        {
            Run(c_Links[it_Index].Command);
        }
    }
}

void StartScreen::Run(std::string_view command) const
{
    if (const Shortcuts::Command* l_Command = m_Session.GetShortcuts().Find(command); l_Command != nullptr && l_Command->Run)
    {
        l_Command->Run();
    }
}