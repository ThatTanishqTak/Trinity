#include "Trinity/trpch.h"
#include "EditorLayer.h"

#include <imgui.h>
#include "Trinity/ECS/SceneSerializer.h"
#include "Trinity/ECS/Components.h"
#include "Trinity/ECS/Entity.h"
#include "Trinity/Renderer/Primitives.h"

EditorLayer::EditorLayer(Trinity::Scene* scene, Trinity::AssetManager* assetManager) : m_Scene(scene), m_AssetManager(assetManager)
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
                Trinity::SceneSerializer l_Serializer(m_Scene, m_AssetManager);
                l_Serializer.Serialize("scene.yaml");
            }

            if (ImGui::MenuItem("Load Scene"))
            {
                Trinity::SceneSerializer l_Serializer(m_Scene, m_AssetManager);
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

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("Create Primitive");
    }

    if (ImGui::BeginPopup("Create Primitive"))
    {
        if (ImGui::MenuItem("Cube"))
        {
            if (m_Scene && m_AssetManager)
            {
                Trinity::Entity l_Entity = m_Scene->CreateEntity();
                auto& a_Mesh = l_Entity.AddComponent<Trinity::MeshComponent>();
                a_Mesh.MeshHandle = m_AssetManager->CreatePrimitiveMesh(Trinity::PrimitiveType::Cube);
                l_Entity.AddComponent<Trinity::MaterialComponent>();
            }
        }

        if (ImGui::MenuItem("Sphere"))
        {
            if (m_Scene && m_AssetManager)
            {
                Trinity::Entity l_Entity = m_Scene->CreateEntity();
                auto& a_Mesh = l_Entity.AddComponent<Trinity::MeshComponent>();
                a_Mesh.MeshHandle = m_AssetManager->CreatePrimitiveMesh(Trinity::PrimitiveType::Sphere);
                l_Entity.AddComponent<Trinity::MaterialComponent>();
            }
        }

        if (ImGui::MenuItem("Square"))
        {
            if (m_Scene && m_AssetManager)
            {
                Trinity::Entity l_Entity = m_Scene->CreateEntity();
                auto& a_Mesh = l_Entity.AddComponent<Trinity::MeshComponent>();
                a_Mesh.MeshHandle = m_AssetManager->CreatePrimitiveMesh(Trinity::PrimitiveType::Square);
                l_Entity.AddComponent<Trinity::MaterialComponent>();
            }
        }

        if (ImGui::MenuItem("Circle"))
        {
            if (m_Scene && m_AssetManager)
            {
                Trinity::Entity l_Entity = m_Scene->CreateEntity();
                auto& a_Mesh = l_Entity.AddComponent<Trinity::MeshComponent>();
                a_Mesh.MeshHandle = m_AssetManager->CreatePrimitiveMesh(Trinity::PrimitiveType::Circle);
                l_Entity.AddComponent<Trinity::MaterialComponent>();
            }
        }
        ImGui::EndPopup();
    }

    for (auto& it_Panel : m_Panels)
    {
        it_Panel->OnUIRender();
    }

    ImGui::End();
}

void EditorLayer::RegisterPanel(std::unique_ptr<Trinity::Panel> panel)
{
    m_Panels.emplace_back(std::move(panel));
}