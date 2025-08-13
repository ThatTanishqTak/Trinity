#include "Trinity/trpch.h"
#include "InspectorPanel.h"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include "Trinity/ECS/Components.h"
#include "Trinity/Utilities/Utilities.h"

InspectorPanel::InspectorPanel(Trinity::Scene* context, entt::entity* selectionContext, Trinity::AssetManager* assetManager) : m_Context(context), m_SelectionContext(selectionContext),
m_AssetManager(assetManager)
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
            auto& a_Mesh = l_Registry.get<Trinity::MeshComponent>(*m_SelectionContext);
            if (ImGui::TreeNode("Mesh"))
            {
                ImGui::TextUnformatted(a_Mesh.MeshPath.c_str());
                if (ImGui::Button("Select Mesh"))
                {
                    std::string l_Path = Trinity::Utilities::FileManagement::OpenFile();
                    if (!l_Path.empty())
                    {
                        a_Mesh.MeshPath = l_Path;
                        if (m_AssetManager)
                        {
                            a_Mesh.MeshHandle = m_AssetManager->RequestAsset<Trinity::Mesh>(a_Mesh.MeshPath);
                        }
                    }
                }
                ImGui::TextUnformatted(a_Mesh.TexturePath.c_str());
                if (ImGui::Button("Select Texture"))
                {
                    std::string l_Path = Trinity::Utilities::FileManagement::OpenFile();
                    if (!l_Path.empty())
                    {
                        a_Mesh.TexturePath = l_Path;
                        if (m_AssetManager)
                        {
                            a_Mesh.MeshTexture = m_AssetManager->RequestAsset<Trinity::Texture>(a_Mesh.TexturePath);
                        }
                    }
                }
                ImGui::TreePop();
            }
        }

        if (l_Registry.any_of<Trinity::LightComponent>(*m_SelectionContext))
        {
            auto& a_Light = l_Registry.get<Trinity::LightComponent>(*m_SelectionContext);
            if (ImGui::TreeNode("Light"))
            {
                ImGui::ColorEdit3("Color", glm::value_ptr(a_Light.Color));
                ImGui::DragFloat("Intensity", &a_Light.Intensity, 0.1f, 0.0f, 100.0f);
                const char* l_Types[] = { "Directional", "Point", "Spot" };
                int l_Type = static_cast<int>(a_Light.LightType);
                if (ImGui::Combo("Type", &l_Type, l_Types, IM_ARRAYSIZE(l_Types)))
                {
                    a_Light.LightType = static_cast<Trinity::LightComponent::Type>(l_Type);
                }
                ImGui::TreePop();
            }
        }

        if (l_Registry.any_of<Trinity::MaterialComponent>(*m_SelectionContext))
        {
            auto& a_Material = l_Registry.get<Trinity::MaterialComponent>(*m_SelectionContext);
            if (ImGui::TreeNode("Material"))
            {
                ImGui::ColorEdit3("Albedo", glm::value_ptr(a_Material.Albedo));
                ImGui::DragFloat("Roughness", &a_Material.Roughness, 0.01f, 0.0f, 1.0f);
                ImGui::DragFloat("Metallic", &a_Material.Metallic, 0.01f, 0.0f, 1.0f);
                ImGui::DragFloat("Specular", &a_Material.Specular, 0.01f, 0.0f, 1.0f);

                ImGui::TextUnformatted(a_Material.NormalMapPath.c_str());
                if (ImGui::Button("Normal Map"))
                {
                    std::string l_Path = Trinity::Utilities::FileManagement::OpenFile();
                    if (!l_Path.empty())
                    {
                        a_Material.NormalMapPath = l_Path;
                        if (m_AssetManager)
                        {
                            a_Material.NormalMap = m_AssetManager->RequestAsset<Trinity::Texture>(a_Material.NormalMapPath);
                        }
                    }
                }
                ImGui::TextUnformatted(a_Material.RoughnessMapPath.c_str());
                if (ImGui::Button("Roughness Map"))
                {
                    std::string l_Path = Trinity::Utilities::FileManagement::OpenFile();
                    if (!l_Path.empty())
                    {
                        a_Material.RoughnessMapPath = l_Path;
                        if (m_AssetManager)
                        {
                            a_Material.RoughnessMap = m_AssetManager->RequestAsset<Trinity::Texture>(a_Material.RoughnessMapPath);
                        }
                    }
                }
                ImGui::TextUnformatted(a_Material.MetallicMapPath.c_str());
                if (ImGui::Button("Metallic Map"))
                {
                    std::string l_Path = Trinity::Utilities::FileManagement::OpenFile();
                    if (!l_Path.empty())
                    {
                        a_Material.MetallicMapPath = l_Path;
                        if (m_AssetManager)
                        {
                            a_Material.MetallicMap = m_AssetManager->RequestAsset<Trinity::Texture>(a_Material.MetallicMapPath);
                        }
                    }
                }

                ImGui::TreePop();
            }
        }
    }

    ImGui::End();
}