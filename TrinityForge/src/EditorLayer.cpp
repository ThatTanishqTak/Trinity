#include "Trinity/trpch.h"
#include "EditorLayer.h"

#include <imgui.h>
#include "Trinity/ECS/SceneSerializer.h"

EditorLayer::EditorLayer(Trinity::Scene* p_Scene, Trinity::ResourceManager* resourceManager) : m_Scene(p_Scene), m_ResourceManager(resourceManager)
{

}

void EditorLayer::OnUIRender()
{
    ImGuiWindowFlags l_WindowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    const ImGuiViewport* l_Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(l_Viewport->WorkPos);
    ImGui::SetNextWindowSize(l_Viewport->WorkSize);
    ImGui::SetNextWindowViewport(l_Viewport->ID);

    l_WindowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGui::Begin("DockSpace", nullptr, l_WindowFlags);

    ImGui::PopStyleVar(2);

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save Scene"))
            {
                Trinity::SceneSerializer l_Serializer(m_Scene, m_ResourceManager);
                l_Serializer.Serialize("scene.yaml");
            }

            if (ImGui::MenuItem("Load Scene"))
            {
                Trinity::SceneSerializer l_Serializer(m_Scene, m_ResourceManager);
                l_Serializer.Deserialize("scene.yaml");
            }

            if (ImGui::MenuItem("Exit"))
            {

            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGuiID l_DockspaceID = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(l_DockspaceID, ImVec2(0.0f, 0.0f));

    for (auto& it_Panel : m_Panels)
    {
        it_Panel->OnUIRender();
    }

    ImGui::End();
}

void EditorLayer::RegisterPanel(std::unique_ptr<Trinity::Panel> p_Panel)
{
    m_Panels.emplace_back(std::move(p_Panel));
}