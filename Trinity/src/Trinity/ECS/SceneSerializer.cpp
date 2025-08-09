#include "Trinity/trpch.h"

#include "SceneSerializer.h"

#include "Trinity/ECS/Scene.h"
#include "Trinity/ECS/Entity.h"
#include "Trinity/ECS/Components.h"
#include "Trinity/Core/ResourceManager.h"

#include <entt/entt.hpp>
#include <yaml-cpp/yaml.h>
#include <fstream>

namespace Trinity
{
    SceneSerializer::SceneSerializer(Scene* scene, ResourceManager* resourceManager) : m_Scene(scene), m_ResourceManager(resourceManager)
    {

    }

    void SceneSerializer::Serialize(const std::filesystem::path& path)
    {
        YAML::Emitter l_Out;
        l_Out << YAML::BeginMap;
        l_Out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

        auto& a_Registry = m_Scene->GetRegistry();

        // Iterate directly over the entity storage
        auto& entityStorage = a_Registry.storage<entt::entity>();
        for (entt::entity it_EntityID : entityStorage)
        {
            l_Out << YAML::BeginMap;
            l_Out << YAML::Key << "Entity" << YAML::Value << static_cast<uint32_t>(it_EntityID);

            if (a_Registry.any_of<TransformComponent>(it_EntityID))
            {
                auto& a_Transform = a_Registry.get<TransformComponent>(it_EntityID);
                l_Out << YAML::Key << "TransformComponent";
                l_Out << YAML::BeginMap;
                l_Out << YAML::Key << "Translation" << YAML::Value << YAML::Flow << YAML::BeginSeq
                    << a_Transform.Translation.x << a_Transform.Translation.y << a_Transform.Translation.z << YAML::EndSeq;
                l_Out << YAML::Key << "Rotation" << YAML::Value << YAML::Flow << YAML::BeginSeq
                    << a_Transform.Rotation.x << a_Transform.Rotation.y << a_Transform.Rotation.z << YAML::EndSeq;
                l_Out << YAML::Key << "Scale" << YAML::Value << YAML::Flow << YAML::BeginSeq
                    << a_Transform.Scale.x << a_Transform.Scale.y << a_Transform.Scale.z << YAML::EndSeq;
                l_Out << YAML::EndMap;
            }

            if (a_Registry.any_of<MeshComponent>(it_EntityID))
            {
                auto& a_Mesh = a_Registry.get<MeshComponent>(it_EntityID);
                l_Out << YAML::Key << "MeshComponent";
                l_Out << YAML::BeginMap;
                l_Out << YAML::Key << "MeshPath" << YAML::Value << a_Mesh.MeshPath;
                l_Out << YAML::Key << "TexturePath" << YAML::Value << a_Mesh.TexturePath;
                l_Out << YAML::EndMap;
            }

            if (a_Registry.any_of<LightComponent>(it_EntityID))
            {
                auto& a_Light = a_Registry.get<LightComponent>(it_EntityID);
                l_Out << YAML::Key << "LightComponent";
                l_Out << YAML::BeginMap;
                l_Out << YAML::Key << "Color" << YAML::Value << YAML::Flow << YAML::BeginSeq << a_Light.Color.r << a_Light.Color.g << a_Light.Color.b << YAML::EndSeq;
                l_Out << YAML::Key << "Intensity" << YAML::Value << a_Light.Intensity;
                l_Out << YAML::Key << "Type" << YAML::Value << static_cast<int>(a_Light.LightType);
                l_Out << YAML::EndMap;
            }

            if (a_Registry.any_of<MaterialComponent>(it_EntityID))
            {
                auto& a_Material = a_Registry.get<MaterialComponent>(it_EntityID);
                l_Out << YAML::Key << "MaterialComponent";
                l_Out << YAML::BeginMap;
                l_Out << YAML::Key << "Albedo" << YAML::Value << YAML::Flow << YAML::BeginSeq << a_Material.Albedo.r << a_Material.Albedo.g << a_Material.Albedo.b << YAML::EndSeq;
                l_Out << YAML::Key << "Roughness" << YAML::Value << a_Material.Roughness;
                l_Out << YAML::Key << "Metallic" << YAML::Value << a_Material.Metallic;
                l_Out << YAML::Key << "Specular" << YAML::Value << a_Material.Specular;
                l_Out << YAML::Key << "NormalMapPath" << YAML::Value << a_Material.NormalMapPath;
                l_Out << YAML::Key << "RoughnessMapPath" << YAML::Value << a_Material.RoughnessMapPath;
                l_Out << YAML::Key << "MetallicMapPath" << YAML::Value << a_Material.MetallicMapPath;
                l_Out << YAML::EndMap;
            }

            l_Out << YAML::EndMap; // entity map
        }

        l_Out << YAML::EndSeq; // Entities
        l_Out << YAML::EndMap;

        std::ofstream l_File(path);
        l_File << l_Out.c_str();
    }

    bool SceneSerializer::Deserialize(const std::filesystem::path& path)
    {
        YAML::Node l_Data = YAML::LoadFile(path.string());
        if (!l_Data["Entities"])
        {
            return false;
        }

        auto& a_Registry = m_Scene->GetRegistry();
        a_Registry.clear();

        auto a_Entities = l_Data["Entities"];
        for (auto it_Entity : a_Entities)
        {
            entt::entity it_EntityID = a_Registry.create();
            Entity e(it_EntityID, &a_Registry);

            if (auto a_TransformNode = it_Entity["TransformComponent"])
            {
                auto& a_Transform = e.AddComponent<TransformComponent>();
                auto a_Translation = a_TransformNode["Translation"];
                a_Transform.Translation = { a_Translation[0].as<float>(), a_Translation[1].as<float>(), a_Translation[2].as<float>() };
                auto a_Rotation = a_TransformNode["Rotation"];
                a_Transform.Rotation = { a_Rotation[0].as<float>(), a_Rotation[1].as<float>(), a_Rotation[2].as<float>() };
                auto a_Scale = a_TransformNode["Scale"];
                a_Transform.Scale = { a_Scale[0].as<float>(), a_Scale[1].as<float>(), a_Scale[2].as<float>() };
            }

            if (auto a_MeshNode = it_Entity["MeshComponent"])
            {
                auto& a_Mesh = e.AddComponent<MeshComponent>();
                a_Mesh.MeshPath = a_MeshNode["MeshPath"].as<std::string>();
                a_Mesh.TexturePath = a_MeshNode["TexturePath"].as<std::string>();

                if (m_ResourceManager && !a_Mesh.MeshPath.empty())
                {
                    a_Mesh.MeshHandle = m_ResourceManager->Load<Mesh>(a_Mesh.MeshPath).get();
                }

                if (m_ResourceManager && !a_Mesh.TexturePath.empty())
                {
                    a_Mesh.MeshTexture = m_ResourceManager->Load<Texture>(a_Mesh.TexturePath).get();
                }
            }

            if (auto a_LightNode = it_Entity["LightComponent"])
            {
                auto& a_Light = e.AddComponent<LightComponent>();
                auto a_Color = a_LightNode["Color"];
                a_Light.Color = { a_Color[0].as<float>(), a_Color[1].as<float>(), a_Color[2].as<float>() };
                a_Light.Intensity = a_LightNode["Intensity"].as<float>();
                a_Light.LightType = static_cast<LightComponent::Type>(a_LightNode["Type"].as<int>());
            }

            if (auto a_MaterialNode = it_Entity["MaterialComponent"])
            {
                auto& a_Material = e.AddComponent<MaterialComponent>();
                auto a_Albedo = a_MaterialNode["Albedo"];
                a_Material.Albedo = { a_Albedo[0].as<float>(), a_Albedo[1].as<float>(), a_Albedo[2].as<float>() };
                a_Material.Roughness = a_MaterialNode["Roughness"].as<float>();
                a_Material.Metallic = a_MaterialNode["Metallic"].as<float>();
                a_Material.Specular = a_MaterialNode["Specular"].as<float>();
                a_Material.NormalMapPath = a_MaterialNode["NormalMapPath"].as<std::string>();
                a_Material.RoughnessMapPath = a_MaterialNode["RoughnessMapPath"].as<std::string>();
                a_Material.MetallicMapPath = a_MaterialNode["MetallicMapPath"].as<std::string>();

                if (m_ResourceManager)
                {
                    if (!a_Material.NormalMapPath.empty())
                    {
                        a_Material.NormalMap = m_ResourceManager->Load<Texture>(a_Material.NormalMapPath).get();
                    }

                    if (!a_Material.RoughnessMapPath.empty())
                    {
                        a_Material.RoughnessMap = m_ResourceManager->Load<Texture>(a_Material.RoughnessMapPath).get();
                    }

                    if (!a_Material.MetallicMapPath.empty())
                    {
                        a_Material.MetallicMap = m_ResourceManager->Load<Texture>(a_Material.MetallicMapPath).get();
                    }
                }
            }
        }

        return true;
    }
}