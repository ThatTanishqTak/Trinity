#include "Panels/ViewportPanel.hpp"

#include <Trinity.hpp>

#include <imgui.h>

ViewportPanel::ViewportPanel() : Panel("Viewport", Trinity::Icons::c_Monitor, DockSlot::Centre)
{

}

void ViewportPanel::OnImGuiRender()
{
    ImGui::TextDisabled("The scene view goes here, drawn from the renderer's scene target");
}