#include "Trinity/trpch.h"
#include "SceneHierarchyPanel.h"

#include <imgui.h>
#include "Trinity/ECS/Components.h"

SceneHierarchyPanel::SceneHierarchyPanel(Trinity::Scene* p_Context, entt::entity* p_SelectionContext)
    : m_Context(p_Context), m_SelectionContext(p_SelectionContext)
{
}

void SceneHierarchyPanel::OnUIRender()
{
    ImGui::Begin("Scene Hierarchy");

    if (m_Context)
    {
        auto l_View = m_Context->GetRegistry().view<Trinity::TransformComponent>();
        for (auto [it_Entity, a_Transform] : l_View.each())
        {
            DrawEntityNode(it_Entity);
        }
    }

    ImGui::End();
}

void SceneHierarchyPanel::DrawEntityNode(entt::entity p_Entity)
{
    ImGuiTreeNodeFlags l_Flags = ((m_SelectionContext && *m_SelectionContext == p_Entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
    bool l_Opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(p_Entity))), l_Flags, "Entity %u", static_cast<uint32_t>(p_Entity));

    if (ImGui::IsItemClicked())
    {
        if (m_SelectionContext)
        {
            *m_SelectionContext = p_Entity;
        }
    }

    if (l_Opened)
    {
        ImGui::TreePop();
    }
}