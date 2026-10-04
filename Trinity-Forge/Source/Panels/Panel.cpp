#include "Panels/Panel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <format>

Panel::Panel(std::string_view title, std::string_view icon, DockSlot slot) : m_Title(title), m_WindowName(std::format("{} {}###{}", icon, title, title)), m_MenuLabel(std::format("{} {}", icon, title)), m_Slot(slot)
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

    bool l_Open = true;
    if (ImGui::Begin(m_WindowName.c_str(), &l_Open))
    {
        OnImGuiRender();
    }

    ImGui::End();
    SetOpen(l_Open);
}