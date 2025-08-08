#include "Trinity/trpch.h"
#include "SceneViewportPanel.h"

#include <imgui.h>

SceneViewportPanel::SceneViewportPanel(Trinity::Renderer* renderer) : m_Renderer(renderer)
{

}

void SceneViewportPanel::OnUIRender()
{
    ImGui::Begin("Viewport");

    ImVec2 l_ViewportPanelSize = ImGui::GetContentRegionAvail();
    m_ViewportSize = { l_ViewportPanelSize.x, l_ViewportPanelSize.y };

    if (m_Renderer)
    {
        ImTextureID l_TextureID = m_Renderer->GetViewportImage();
        if (l_TextureID)
        {
            ImGui::Image(l_TextureID, l_ViewportPanelSize);
        }
    }

    ImGui::End();
}