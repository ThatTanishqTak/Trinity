#include "Panels/ConsolePanel.hpp"

#include <Trinity.hpp>

#include <imgui.h>

ConsolePanel::ConsolePanel() : Panel("Console", Trinity::Icons::c_Terminal, DockSlot::Bottom)
{

}

void ConsolePanel::OnImGuiRender()
{
    ImGui::TextDisabled("Log lines and the command line go here");
}