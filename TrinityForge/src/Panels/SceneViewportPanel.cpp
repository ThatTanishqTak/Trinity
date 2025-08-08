#include "Trinity/trpch.h"
#include "SceneViewportPanel.h"
#include "Trinity/ECS/Components.h"

#include <ImGuizmo.h>
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

SceneViewportPanel::SceneViewportPanel(Trinity::Renderer* renderer, Trinity::Scene* context, entt::entity* selectionContext) : m_Renderer(renderer), m_Context(context), 
    m_SelectionContext(selectionContext)
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

        if (m_Context && m_SelectionContext && *m_SelectionContext != entt::null)
        {
            auto& l_Registry = m_Context->GetRegistry();
            if (l_Registry.any_of<Trinity::TransformComponent>(*m_SelectionContext))
            {
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::BeginFrame();
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, m_ViewportSize.x, m_ViewportSize.y);

                glm::mat4 l_View = m_Renderer->GetCamera().GetViewMatrix();
                glm::mat4 l_Projection = m_Renderer->GetCamera().GetProjectionMatrix();

                auto& a_Transform = l_Registry.get<Trinity::TransformComponent>(*m_SelectionContext);
                glm::mat4 l_Transform = a_Transform.GetTransform();

                ImGuizmo::Manipulate(glm::value_ptr(l_View), glm::value_ptr(l_Projection), ImGuizmo::TRANSLATE, ImGuizmo::LOCAL, glm::value_ptr(l_Transform));

                if (ImGuizmo::IsUsing())
                {
                    glm::vec3 l_Translation, l_Rotation, l_Scale;
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(l_Transform), glm::value_ptr(l_Translation), glm::value_ptr(l_Rotation), glm::value_ptr(l_Scale));
                    a_Transform.Translation = l_Translation;
                    a_Transform.Rotation = l_Rotation;
                    a_Transform.Scale = l_Scale;
                }
            }
        }
    }

    ImGui::End();
}