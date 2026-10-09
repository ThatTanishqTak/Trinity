#include "Panels/Panel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <format>
#include <utility>

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

void Panel::Draw()
{
    if (!m_Open)
    {
        return;
    }

    // A borderless panel fills its window edge to edge and never scrolls, so what it draws can match the window in pixels
    if (m_Borderless)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    }

    if (std::exchange(m_FocusRequested, false))
    {
        ImGui::SetNextWindowFocus();
    }

    bool l_Open = true;
    const bool l_Visible = ImGui::Begin(m_WindowName.c_str(), &l_Open, m_Borderless ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None);
    if (m_Borderless)
    {
        ImGui::PopStyleVar();
    }

    if (l_Visible)
    {
        OnImGuiRender();
    }

    ImGui::End();
    SetOpen(l_Open);
}