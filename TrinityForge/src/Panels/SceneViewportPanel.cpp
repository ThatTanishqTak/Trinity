#include "Trinity/trpch.h"
#include "SceneViewportPanel.h"

#include "Trinity/Renderer/Renderer.h"
#include "Trinity/ECS/Scene.h"
#include "Trinity/ECS/Components.h"

#include <ImGuizmo.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

SceneViewportPanel::SceneViewportPanel(Trinity::Renderer* renderer, Trinity::Scene* context, entt::entity* selectionContext) : m_Renderer(renderer), m_Context(context), 
    m_SelectionContext(selectionContext)
{

}

void SceneViewportPanel::OnUIRender()
{
    ImGui::Begin("Viewport");

    ImVec2 l_Size = ImGui::GetContentRegionAvail();
    if (m_ViewportSize.x != l_Size.x || m_ViewportSize.y != l_Size.y)
    {
        m_ViewportSize = l_Size;
        if (m_Renderer)
        {
            m_Renderer->OnWindowResize();
        }
    }

    if (m_Renderer)
    {
        ImTextureID l_Image = m_Renderer->GetViewportImage();
        if (l_Image)
        {
            ImGui::Image(l_Image, m_ViewportSize, ImVec2(0, 1), ImVec2(1, 0));
        }
    }

    if (m_Context && m_SelectionContext && *m_SelectionContext != entt::null)
    {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::BeginFrame();
        ImGuizmo::SetDrawlist();
        ImVec2 l_WindowPos = ImGui::GetWindowPos();
        ImGuizmo::SetRect(l_WindowPos.x, l_WindowPos.y, m_ViewportSize.x, m_ViewportSize.y);

        auto& a_Camera = m_Renderer->GetCamera();
        glm::mat4 l_View = a_Camera.GetViewMatrix();
        glm::mat4 l_Projection = a_Camera.GetProjectionMatrix();
        l_Projection[1][1] *= -1.0f;

        auto& l_Registry = m_Context->GetRegistry();
        auto& a_Transform = l_Registry.get<Trinity::TransformComponent>(*m_SelectionContext);
        glm::mat4 l_Transform = glm::translate(glm::mat4(1.0f), a_Transform.Translation) 
                                * glm::toMat4(glm::quat(glm::radians(a_Transform.Rotation)))
                                * glm::scale(glm::mat4(1.0f), a_Transform.Scale);

        if (ImGuizmo::Manipulate(glm::value_ptr(l_View), glm::value_ptr(l_Projection), ImGuizmo::TRANSLATE, ImGuizmo::LOCAL, glm::value_ptr(l_Transform)))
        {
            glm::vec3 l_Translation, l_Rotation, l_Scale;
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(l_Transform), glm::value_ptr(l_Translation), glm::value_ptr(l_Rotation), glm::value_ptr(l_Scale));
            a_Transform.Translation = l_Translation;
            a_Transform.Rotation = l_Rotation;
            a_Transform.Scale = l_Scale;
        }
    }

    ImGui::End();
}