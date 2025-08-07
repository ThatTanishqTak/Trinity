#include "Trinity/trpch.h"

#include "PropertiesPanel.h"

#include <imgui.h>

void PropertiesPanel::OnUIRender()
{
    ImGui::Begin("Properties");
    ImGui::Text("Properties Panel");
    ImGui::End();
}