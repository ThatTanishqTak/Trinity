#include "Panels/HierarchyPanel.hpp"

#include <Trinity.hpp>

#include <imgui.h>

HierarchyPanel::HierarchyPanel() : Panel("Hierarchy", Trinity::Icons::c_Sitemap, DockSlot::Left)
{

}

void HierarchyPanel::OnImGuiRender()
{
    ImGui::TextDisabled("The scene's entities go here, once Forge has a scene");
}