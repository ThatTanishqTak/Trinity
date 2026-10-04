#include "Panels/PropertiesPanel.hpp"

#include <Trinity.hpp>

#include <imgui.h>

PropertiesPanel::PropertiesPanel() : Panel("Properties", Trinity::Icons::c_Sliders, DockSlot::Right)
{

}

void PropertiesPanel::OnImGuiRender()
{
    ImGui::TextDisabled("The selected entity's components go here");
}