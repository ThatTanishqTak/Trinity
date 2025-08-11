#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <future>
#include <thread>
#include <chrono>

#include "Trinity/Core/ResourceManager.h"
#include "Trinity/ECS/Components.h"

namespace Trinity
{
    class Scene;
    class VulkanContext;

    class AssetManager
    {
    public:
        enum class Type
        {
            Texture,
            Mesh
        };

        explicit AssetManager(VulkanContext* context);

        void RegisterSceneAssets(Scene* scene);

        template<typename T>
        std::shared_ptr<T> RequestAsset(const std::string& path);

        void ProcessJobs() { m_ResourceManager->ProcessJobs(); }
        std::shared_ptr<Mesh> CreatePlaceholderMesh() { return m_ResourceManager->CreatePlaceholderMesh(); }

    private:
        template<typename T>
        std::shared_ptr<T> WaitForLoad(const std::string& path, std::future<std::shared_ptr<T>>& future);

        std::unique_ptr<ResourceManager> m_ResourceManager;
        std::unordered_map<std::string, Type> m_Registry;
    };

    template<>
    inline std::shared_ptr<Texture> AssetManager::RequestAsset<Texture>(const std::string& path)
    {
        if (m_Registry.find(path) == m_Registry.end())
        {
            m_Registry[path] = Type::Texture;
        }
        auto l_Future = m_ResourceManager->Load<Texture>(path, ResourceManager::DecodeTexture);
        return WaitForLoad(path, l_Future);
    }

    template<>
    inline std::shared_ptr<Mesh> AssetManager::RequestAsset<Mesh>(const std::string& path)
    {
        if (m_Registry.find(path) == m_Registry.end())
        {
            m_Registry[path] = Type::Mesh;
        }
        auto l_Future = m_ResourceManager->Load<Mesh>(path, ResourceManager::DecodeMesh);
        return WaitForLoad(path, l_Future);
    }

    template<typename T>
    inline std::shared_ptr<T> AssetManager::WaitForLoad(const std::string& path, std::future<std::shared_ptr<T>>& future)
    {
        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        {
            m_ResourceManager->ProcessJobs();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return future.get();
    }
}