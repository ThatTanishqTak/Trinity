#include "Trinity/trpch.h"
#include "SceneHierarchyPanel.h"

#include <imgui.h>
#include "Trinity/ECS/Components.h"

SceneHierarchyPanel::SceneHierarchyPanel(Trinity::Scene* context, entt::entity* selectionContext) : m_Context(context), m_SelectionContext(selectionContext)
{

}

void SceneHierarchyPanel::OnUIRender()
{
    ImGui::Begin("Scene Hierarchy");

    if (m_Context)
    {
        auto a_View = m_Context->GetRegistry().view<Trinity::TransformComponent>();
        for (auto [it_Entity, it_Transform] : a_View.each())
        {
            DrawEntityNode(it_Entity);
        }
    }

    ImGui::End();
}

void SceneHierarchyPanel::DrawEntityNode(entt::entity entity)
{
    ImGuiTreeNodeFlags l_Flags = ((m_SelectionContext && *m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
    bool l_Opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(entity))), l_Flags, "Entity %u", static_cast<uint32_t>(entity));

    if (ImGui::IsItemClicked())
    {
        if (m_SelectionContext)
        {
            *m_SelectionContext = entity;
        }
    }

    if (l_Opened)
    {
        ImGui::TreePop();
    }
}