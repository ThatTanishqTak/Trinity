#include "Trinity/trpch.h"
#include "AssetManager.h"

#include "Trinity/ECS/Scene.h"

#include <entt/entt.hpp>

namespace Trinity
{
    AssetManager::AssetManager(VulkanContext* context)
        : m_ResourceManager(std::make_unique<ResourceManager>(context))
    {
    }

    void AssetManager::RegisterSceneAssets(Scene* scene)
    {
        if (!scene)
        {
            return;
        }

        std::unordered_set<std::string> l_CurrentPaths;
        auto& a_Registry = scene->GetRegistry();

        auto meshView = a_Registry.view<MeshComponent>();
        for (auto entity : meshView)
        {
            auto& a_Mesh = meshView.get<MeshComponent>(entity);
            if (!a_Mesh.MeshPath.empty())
            {
                a_Mesh.MeshHandle = RequestAsset<Mesh>(a_Mesh.MeshPath);
                l_CurrentPaths.insert(a_Mesh.MeshPath);
            }
            if (!a_Mesh.TexturePath.empty())
            {
                a_Mesh.MeshTexture = RequestAsset<Texture>(a_Mesh.TexturePath);
                l_CurrentPaths.insert(a_Mesh.TexturePath);
            }
        }

        auto materialView = a_Registry.view<MaterialComponent>();
        for (auto entity : materialView)
        {
            auto& a_Material = materialView.get<MaterialComponent>(entity);
            if (!a_Material.NormalMapPath.empty())
            {
                a_Material.NormalMap = RequestAsset<Texture>(a_Material.NormalMapPath);
                l_CurrentPaths.insert(a_Material.NormalMapPath);
            }
            if (!a_Material.RoughnessMapPath.empty())
            {
                a_Material.RoughnessMap = RequestAsset<Texture>(a_Material.RoughnessMapPath);
                l_CurrentPaths.insert(a_Material.RoughnessMapPath);
            }
            if (!a_Material.MetallicMapPath.empty())
            {
                a_Material.MetallicMap = RequestAsset<Texture>(a_Material.MetallicMapPath);
                l_CurrentPaths.insert(a_Material.MetallicMapPath);
            }
        }

        for (auto it = m_Registry.begin(); it != m_Registry.end();)
        {
            if (l_CurrentPaths.find(it->first) == l_CurrentPaths.end())
            {
                if (it->second == Type::Texture)
                {
                    m_ResourceManager->Unload<Texture>(it->first);
                }
                else
                {
                    m_ResourceManager->Unload<Mesh>(it->first);
                }
                it = m_Registry.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}