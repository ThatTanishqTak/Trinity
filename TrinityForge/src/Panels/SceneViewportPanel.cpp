#include "Trinity/trpch.h"
#include "SceneViewportPanel.h"

#include <backends/imgui_impl_vulkan.h>

SceneViewportPanel::SceneViewportPanel(Trinity::Renderer* renderer) : m_Renderer(renderer)
{
    if (m_Renderer)
    {
        auto& l_Texture = m_Renderer->GetTexture();
        m_Image = (ImTextureID)ImGui_ImplVulkan_AddTexture(l_Texture.GetSampler(), l_Texture.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

void SceneViewportPanel::OnUIRender()
{
    ImGui::Begin("Viewport");
    ImVec2 l_Size = ImGui::GetContentRegionAvail();
    if (m_Image)
    {
        ImGui::Image(m_Image, l_Size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }
    ImGui::End();
}