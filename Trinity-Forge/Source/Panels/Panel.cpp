#include "Panels/Panel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <format>
#include <utility>

namespace
{
    // As wide and as tall as the Preferences window opens
    constexpr float c_FloatingWidthInFonts = 46.0f;
    constexpr float c_FloatingHeightInFonts = 24.0f;
}

Panel::Panel(std::string_view title, std::string_view icon, DockSlot slot, std::string_view id) : m_Title(title), m_SettingsID(id.empty() ? title : id), m_WindowName(std::format("{} {}###{}", icon, title, m_SettingsID)), m_MenuLabel(std::format("{} {}", icon, title)), m_Slot(slot)
{

}

// Whether a panel is open is saved in imgui.ini with the layout
void Panel::SetOpen(bool open)
{
    if (open != m_Open)
    {
        m_Open = open;
        ImGui::MarkIniSettingsDirty();
    }
}

void Panel::Skip()
{
    m_WindowHovered = false;
    m_WindowFocused = false;
}

void Panel::Draw()
{
    if (!m_Open)
    {
        Skip();

        return;
    }

    // A borderless panel fills its window edge to edge and never scrolls, to match it in pixels
    if (m_Borderless)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    }

    if (std::exchange(m_FocusRequested, false))
    {
        ImGui::SetNextWindowFocus();
    }

    // Centred at a size its settings fit in the first time, then where imgui.ini keeps it, docked or not
    if (m_Slot == DockSlot::Floating)
    {
        const float l_Font = ImGui::GetFontSize();
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(l_Font * c_FloatingWidthInFonts, l_Font * c_FloatingHeightInFonts), ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags l_Flags = m_Borderless ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None;
    l_Flags |= m_Navigable ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoNavInputs;

    bool l_Open = true;
    const bool l_Visible = ImGui::Begin(m_WindowName.c_str(), &l_Open, l_Flags);
    if (m_Borderless)
    {
        ImGui::PopStyleVar();
    }

    // Over its children too, and while something in it is dragged
    m_WindowHovered = l_Visible && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    m_WindowFocused = l_Visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

    if (l_Visible)
    {
        OnImGuiRender();
    }

    ImGui::End();
    SetOpen(l_Open);
}