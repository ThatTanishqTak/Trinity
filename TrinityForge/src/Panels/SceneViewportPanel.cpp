#include "Trinity/trpch.h"

#include "SceneViewportPanel.h"

#include "Trinity/Renderer/Renderer.h"

SceneViewportPanel::SceneViewportPanel(Trinity::Renderer* renderer) : m_Renderer(renderer)
{

}

void SceneViewportPanel::OnUIRender()
{
    ImGui::Begin("Viewport");

    ImDrawList* l_DrawList = ImGui::GetWindowDrawList();

    if (ImGui::BeginPopupContextWindow())
    {
        if (ImGui::BeginMenu("Add"))
        {
            if (ImGui::MenuItem("Rectangle"))
            {
                ImVec2 l_MousePos = ImGui::GetMousePos();
                m_Primitives.push_back({ PrimitiveType::Rectangle, l_MousePos, ImVec2(50.0f, 50.0f) });
            }
        
            if (ImGui::MenuItem("Circle"))
            {
                ImVec2 l_MousePos = ImGui::GetMousePos();
                m_Primitives.push_back({ PrimitiveType::Circle, l_MousePos, ImVec2(50.0f, 0.0f) });
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    for (auto& a_Primitive : m_Primitives)
    {
        switch (a_Primitive.m_Type)
        {
        case PrimitiveType::Rectangle:
        {
            ImVec2 l_HalfSize = { a_Primitive.m_Size.x * 0.5f, a_Primitive.m_Size.y * 0.5f };
            ImVec2 l_Min = { a_Primitive.m_Position.x - l_HalfSize.x, a_Primitive.m_Position.y - l_HalfSize.y };
            ImVec2 l_Max = { a_Primitive.m_Position.x + l_HalfSize.x, a_Primitive.m_Position.y + l_HalfSize.y };
            l_DrawList->AddRect(l_Min, l_Max, IM_COL32(255, 255, 255, 255));

            break;
        }
        case PrimitiveType::Circle:
            l_DrawList->AddCircle(a_Primitive.m_Position, a_Primitive.m_Size.x,
                IM_COL32(255, 255, 255, 255));
            break;
        }
    }

    ImGui::End();
}