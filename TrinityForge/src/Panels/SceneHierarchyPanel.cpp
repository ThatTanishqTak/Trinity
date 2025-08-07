#include "Trinity/trpch.h"

#include "SceneHierarchyPanel.h"

#include <imgui.h>

void SceneHierarchyPanel::OnUIRender()
{
    ImGui::Begin("Scene Hierarchy");
    ImGui::Text("Scene Hierarchy Panel");
    ImGui::End();
}