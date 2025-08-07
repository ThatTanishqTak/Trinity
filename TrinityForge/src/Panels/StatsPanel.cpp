#include "Trinity/trpch.h"

#include "StatsPanel.h"

#include "Trinity/Utilities/Utilities.h"

#include <imgui.h>

void StatsPanel::OnUIRender()
{
    ImGui::Begin("Stats");
    ImGui::Text("FPS: %.2f", Trinity::Utilities::Time::GetFPS());
    ImGui::Text("Frame Time: %.3f ms", Trinity::Utilities::Time::GetDeltaTime() * 1000.0f);
    ImGui::End();
}