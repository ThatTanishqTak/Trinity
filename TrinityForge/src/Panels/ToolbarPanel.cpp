#include "Trinity/trpch.h"

#include "ToolbarPanel.h"

#include <imgui.h>

void ToolbarPanel::OnUIRender()
{
    ImGuiWindowFlags l_WindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking;

    ImGui::Begin("Toolbar", nullptr, l_WindowFlags);

    if (ImGui::Button("Play"))
    {

    }
    
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
    {

    }

    ImGui::End();
}