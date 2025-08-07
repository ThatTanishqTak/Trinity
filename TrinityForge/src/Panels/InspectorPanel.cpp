#include "Trinity/trpch.h"
#include "InspectorPanel.h"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include "Trinity/ECS/Components.h"

InspectorPanel::InspectorPanel(Trinity::Scene* context, entt::entity* selectionContext) : m_Context(context), m_SelectionContext(selectionContext)
{

}

void InspectorPanel::OnUIRender()
{
    ImGui::Begin("Inspector");

    if (m_Context && m_SelectionContext && *m_SelectionContext != entt::null)
    {
        auto& l_Registry = m_Context->GetRegistry();

        if (l_Registry.any_of<Trinity::TransformComponent>(*m_SelectionContext))
        {
            auto& a_Transform = l_Registry.get<Trinity::TransformComponent>(*m_SelectionContext);
            if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::DragFloat3("Translation", glm::value_ptr(a_Transform.Translation), 0.1f);
                ImGui::DragFloat3("Rotation", glm::value_ptr(a_Transform.Rotation), 0.1f);
                ImGui::DragFloat3("Scale", glm::value_ptr(a_Transform.Scale), 0.1f);
                ImGui::TreePop();
            }
        }

        if (l_Registry.any_of<Trinity::MeshComponent>(*m_SelectionContext))
        {
            if (ImGui::TreeNode("Mesh"))
            {
                ImGui::Text("Mesh Component");
                ImGui::TreePop();
            }
        }

        if (l_Registry.any_of<Trinity::LightComponent>(*m_SelectionContext))
        {
            if (ImGui::TreeNode("Light"))
            {
                ImGui::Text("Light Component");
                ImGui::TreePop();
            }
        }

        if (l_Registry.any_of<Trinity::MaterialComponent>(*m_SelectionContext))
        {
            if (ImGui::TreeNode("Material"))
            {
                ImGui::Text("Material Component");
                ImGui::TreePop();
            }
        }
    }

    ImGui::End();
}