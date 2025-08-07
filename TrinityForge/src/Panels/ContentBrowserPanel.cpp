#include "Trinity/trpch.h"

#include "ContentBrowserPanel.h"

#include <imgui.h>

void ContentBrowserPanel::OnUIRender()
{
    ImGui::Begin("Content Browser");
    ImGui::Text("Content Browser Panel");
    ImGui::End();
}