#include "Trinity/trpch.h"
#include "InspectorPanel.h"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include "Trinity/ECS/Components.h"

namespace {
    bool FileDialog(const char* p_PopupName, std::filesystem::path& p_CurrentPath, std::string& p_SelectedPath)
    {
        bool l_Result = false;
        if (ImGui::BeginPopupModal(p_PopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(p_CurrentPath.string().c_str());
            ImGui::Separator();

            if (p_CurrentPath.has_parent_path())
            {
                if (ImGui::Selectable(".."))
                {
                    p_CurrentPath = p_CurrentPath.parent_path();
                }
            }

            for (const auto& l_Entry : std::filesystem::directory_iterator(p_CurrentPath))
            {
                const auto& l_Path = l_Entry.path();
                if (l_Entry.is_directory())
                {
                    std::string l_Label = l_Path.filename().string() + "/";
                    if (ImGui::Selectable(l_Label.c_str()))
                    {
                        p_CurrentPath = l_Path;
                    }
                }
                else
                {
                    bool l_Selected = (p_SelectedPath == l_Path.string());
                    if (ImGui::Selectable(l_Path.filename().string().c_str(), l_Selected))
                    {
                        p_SelectedPath = l_Path.string();
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Select") && !p_SelectedPath.empty())
            {
                l_Result = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                p_SelectedPath.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
        return l_Result;
    }
}

InspectorPanel::InspectorPanel(Trinity::Scene* context, entt::entity* selectionContext, Trinity::ResourceManager* resourceManager)
    : m_Context(context), m_SelectionContext(selectionContext), m_ResourceManager(resourceManager)
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
                    m_FileDialogCurrentPath = std::filesystem::current_path();
                    m_SelectedFile.clear();
                    m_DialogTarget = FileDialogTarget::Mesh;
                    ImGui::OpenPopup("FileDialog");
                }
                ImGui::TextUnformatted(a_Mesh.TexturePath.c_str());
                if (ImGui::Button("Select Texture"))
                {
                    m_FileDialogCurrentPath = std::filesystem::current_path();
                    m_SelectedFile.clear();
                    m_DialogTarget = FileDialogTarget::Texture;
                    ImGui::OpenPopup("FileDialog");
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
                    m_FileDialogCurrentPath = std::filesystem::current_path();
                    m_SelectedFile.clear();
                    m_DialogTarget = FileDialogTarget::NormalMap;
                    ImGui::OpenPopup("FileDialog");
                }
                ImGui::TextUnformatted(a_Material.RoughnessMapPath.c_str());
                if (ImGui::Button("Roughness Map"))
                {
                    m_FileDialogCurrentPath = std::filesystem::current_path();
                    m_SelectedFile.clear();
                    m_DialogTarget = FileDialogTarget::RoughnessMap;
                    ImGui::OpenPopup("FileDialog");
                }
                ImGui::TextUnformatted(a_Material.MetallicMapPath.c_str());
                if (ImGui::Button("Metallic Map"))
                {
                    m_FileDialogCurrentPath = std::filesystem::current_path();
                    m_SelectedFile.clear();
                    m_DialogTarget = FileDialogTarget::MetallicMap;
                    ImGui::OpenPopup("FileDialog");
                }

                ImGui::TreePop();
            }
        }
    }

    if (m_Context && FileDialog("FileDialog", m_FileDialogCurrentPath, m_SelectedFile))
    {
        auto& l_Registry = m_Context->GetRegistry();
        switch (m_DialogTarget)
        {
        case FileDialogTarget::Mesh:
        {
            auto& a_Mesh = l_Registry.get<Trinity::MeshComponent>(*m_SelectionContext);
            a_Mesh.MeshPath = m_SelectedFile;
            if (m_ResourceManager)
            {
                a_Mesh.MeshHandle = m_ResourceManager
                    ->Load<Trinity::Mesh>(a_Mesh.MeshPath, Trinity::ResourceManager::DecodeMesh)
                    .get();
            }
            break;
        }
        case FileDialogTarget::Texture:
        {
            auto& a_Mesh = l_Registry.get<Trinity::MeshComponent>(*m_SelectionContext);
            a_Mesh.TexturePath = m_SelectedFile;
            if (m_ResourceManager)
            {
                a_Mesh.MeshTexture = m_ResourceManager
                    ->Load<Trinity::Texture>(a_Mesh.TexturePath, Trinity::ResourceManager::DecodeTexture)
                    .get();
            }
            break;
        }
        case FileDialogTarget::NormalMap:
        {
            auto& a_Material = l_Registry.get<Trinity::MaterialComponent>(*m_SelectionContext);
            a_Material.NormalMapPath = m_SelectedFile;
            if (m_ResourceManager)
            {
                a_Material.NormalMap = m_ResourceManager
                    ->Load<Trinity::Texture>(a_Material.NormalMapPath, Trinity::ResourceManager::DecodeTexture)
                    .get();
            }
            break;
        }
        case FileDialogTarget::RoughnessMap:
        {
            auto& a_Material = l_Registry.get<Trinity::MaterialComponent>(*m_SelectionContext);
            a_Material.RoughnessMapPath = m_SelectedFile;
            if (m_ResourceManager)
            {
                a_Material.RoughnessMap = m_ResourceManager
                    ->Load<Trinity::Texture>(a_Material.RoughnessMapPath, Trinity::ResourceManager::DecodeTexture)
                    .get();
            }
            break;
        }
        case FileDialogTarget::MetallicMap:
        {
            auto& a_Material = l_Registry.get<Trinity::MaterialComponent>(*m_SelectionContext);
            a_Material.MetallicMapPath = m_SelectedFile;
            if (m_ResourceManager)
            {
                a_Material.MetallicMap = m_ResourceManager
                    ->Load<Trinity::Texture>(a_Material.MetallicMapPath, Trinity::ResourceManager::DecodeTexture)
                    .get();
            }
            break;
        }
        default:
            break;
        }
        m_DialogTarget = FileDialogTarget::None;
    }

    ImGui::End();
}