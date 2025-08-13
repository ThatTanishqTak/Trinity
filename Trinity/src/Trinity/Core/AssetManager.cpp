#include "Trinity/trpch.h"
#include "AssetManager.h"

#include "Trinity/ECS/Scene.h"
#include "Trinity/Renderer/Primitives.h"

#include <entt/entt.hpp>

namespace Trinity
{
    AssetManager::AssetManager(VulkanContext* context) : m_ResourceManager(std::make_unique<ResourceManager>(context))
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

        auto a_MeshView = a_Registry.view<MeshComponent>();
        for (auto it_Entity : a_MeshView)
        {
            auto& a_Mesh = a_MeshView.get<MeshComponent>(it_Entity);
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

        auto a_MaterialView = a_Registry.view<MaterialComponent>();
        for (auto it_Entity : a_MaterialView)
        {
            auto& a_Material = a_MaterialView.get<MaterialComponent>(it_Entity);
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

    std::shared_ptr<Mesh> AssetManager::CreatePrimitiveMesh(PrimitiveType type)
    {
        auto a_Cache = m_PrimitiveMeshCache.find(type);
        if (a_Cache != m_PrimitiveMeshCache.end())
        {
            return a_Cache->second;
        }

        auto a_Mesh = CreatePrimitive(m_ResourceManager->GetContext(), type);
        if (!a_Mesh)
        {
            return nullptr;
        }

        m_PrimitiveMeshCache[type] = a_Mesh;

        return a_Mesh;
    }
}