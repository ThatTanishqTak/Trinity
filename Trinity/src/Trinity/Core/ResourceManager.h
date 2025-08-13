#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <future>
#include <queue>
#include <mutex>
#include <functional>

#include "Trinity/Renderer/Mesh.h"
#include "Trinity/Renderer/Texture.h"

namespace Trinity
{
    class VulkanContext;

    class ResourceManager
    {
    public:
        explicit ResourceManager(VulkanContext* context);

        template<typename T, typename DecodeFunc>
        std::future<std::shared_ptr<T>> Load(const std::string& path, DecodeFunc decode);

        void ProcessJobs();

        std::shared_ptr<Mesh> CreatePlaceholderMesh();

        template<typename T>
        void Unload(const std::string& path);

        static std::function<std::shared_ptr<Texture>(VulkanContext*)> DecodeTexture(const std::string& path);
        static std::function<std::shared_ptr<Mesh>(VulkanContext*)> DecodeMesh(const std::string& path);

    private:
        template<typename T>
        std::unordered_map<std::string, std::shared_ptr<T>>& GetCache();

        VulkanContext* m_Context = nullptr;
        std::unordered_map<std::string, std::shared_ptr<Texture>> m_TextureCache;
        std::unordered_map<std::string, std::shared_ptr<Mesh>> m_MeshCache;
        std::queue<std::function<void()>> m_JobQueue;
        std::mutex m_QueueMutex;
        std::mutex m_CacheMutex;
    };

    template<typename T>
    std::unordered_map<std::string, std::shared_ptr<T>>& ResourceManager::GetCache()
    {
        static_assert(sizeof(T) == 0, "Resource cache not implemented for this type");
    }

    template<>
    inline std::unordered_map<std::string, std::shared_ptr<Texture>>& ResourceManager::GetCache<Texture>()
    {
        return m_TextureCache;
    }

    template<>
    inline std::unordered_map<std::string, std::shared_ptr<Mesh>>& ResourceManager::GetCache<Mesh>()
    {
        return m_MeshCache;
    }

    template<typename T, typename DecodeFunc>
    std::future<std::shared_ptr<T>> ResourceManager::Load(const std::string& path, DecodeFunc decode)
    {
        auto l_Promise = std::make_shared<std::promise<std::shared_ptr<T>>>();
        auto a_Future = l_Promise->get_future();

        {
            std::lock_guard<std::mutex> l_Lock(m_CacheMutex);
            auto& l_Cache = GetCache<T>();
            auto it = l_Cache.find(path);
            if (it != l_Cache.end())
            {
                l_Promise->set_value(it->second);

                return a_Future;
            }
        }

        std::thread([this, path, decode, l_Promise]() mutable
            {
                auto l_Job = decode(path);
                if (!l_Job)
                {
                    l_Promise->set_value(nullptr);
                    return;
                }

                {
                    std::lock_guard<std::mutex> l_QueueLock(m_QueueMutex);
                    m_JobQueue.push([this, path, l_Job = std::move(l_Job), l_Promise]() mutable
                        {
                            auto a_Resource = l_Job(m_Context);
                            if (a_Resource)
                            {
                                std::lock_guard<std::mutex> l_CacheLock(m_CacheMutex);
                                this->template GetCache<T>()[path] = a_Resource;
                            }
                            l_Promise->set_value(a_Resource);
                        });
                }
            }).detach();

        return a_Future;
    }

    template<typename T>
    void ResourceManager::Unload(const std::string& path)
    {
        std::lock_guard<std::mutex> l_Lock(m_CacheMutex);
        GetCache<T>().erase(path);
    }
}