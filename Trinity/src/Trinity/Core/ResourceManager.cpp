#include "Trinity/trpch.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <thread>

#include "Trinity/Core/ResourceManager.h"

#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    ResourceManager::ResourceManager(VulkanContext* context) : m_Context(context)
    {

    }

    void ResourceManager::ProcessJobs()
    {
        std::queue<std::function<void()>> l_LocalQueue;
        {
            std::lock_guard<std::mutex> l_Lock(m_QueueMutex);
            std::swap(l_LocalQueue, m_JobQueue);
        }
        while (!l_LocalQueue.empty())
        {
            l_LocalQueue.front()();
            l_LocalQueue.pop();
        }
    }

    template<>
    std::future<std::shared_ptr<Texture>> ResourceManager::Load<Texture>(const std::string& path)
    {
        auto l_Promise = std::make_shared<std::promise<std::shared_ptr<Texture>>>();
        auto l_Future = l_Promise->get_future();

        {
            std::lock_guard<std::mutex> l_Lock(m_CacheMutex);
            auto it = m_TextureCache.find(path);
            if (it != m_TextureCache.end())
            {
                l_Promise->set_value(it->second);
                return l_Future;
            }
        }

        std::thread([this, path, l_Promise]()
            {
                int l_Width = 0;
                int l_Height = 0;
                std::vector<std::byte> l_Pixels = Utilities::FileManagement::LoadTexture(path, l_Width, l_Height);
                if (l_Pixels.empty())
                {
                    l_Promise->set_value(nullptr);
                    return;
                }

                {
                    std::lock_guard<std::mutex> l_QueueLock(m_QueueMutex);
                    m_JobQueue.push([this, path, l_Pixels = std::move(l_Pixels), l_Width, l_Height, l_Promise]() mutable
                        {
                            auto a_Texture = std::make_shared<Texture>(m_Context);
                            if (auto a_Error = a_Texture->CreateFromPixels(l_Pixels, l_Width, l_Height))
                            {
                                TR_CORE_ERROR("{}", *a_Error);
                                l_Promise->set_value(nullptr);
                                return;
                            }
                            {
                                std::lock_guard<std::mutex> l_CacheLock(m_CacheMutex);
                                m_TextureCache[path] = a_Texture;
                            }
                            l_Promise->set_value(a_Texture);
                        });
                }
            }).detach();

        return l_Future;
    }

    template<>
    std::future<std::shared_ptr<Mesh>> ResourceManager::Load<Mesh>(const std::string& path)
    {
        auto l_Promise = std::make_shared<std::promise<std::shared_ptr<Mesh>>>();
        auto l_Future = l_Promise->get_future();

        {
            std::lock_guard<std::mutex> l_Lock(m_CacheMutex);
            auto it = m_MeshCache.find(path);
            if (it != m_MeshCache.end())
            {
                l_Promise->set_value(it->second);
                return l_Future;
            }
        }

        std::thread([this, path, l_Promise]()
            {
                Assimp::Importer l_Importer;
                const aiScene* l_Scene = l_Importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);
                if (!l_Scene || !l_Scene->mRootNode || l_Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
                {
                    TR_CORE_ERROR("Failed to load mesh: {}", path);
                    l_Promise->set_value(nullptr);
                    return;
                }

                std::vector<Vertex> l_Vertices;
                std::vector<uint32_t> l_Indices;

                if (l_Scene->mNumMeshes > 0)
                {
                    aiMesh* l_MeshData = l_Scene->mMeshes[0];
                    l_Vertices.reserve(l_MeshData->mNumVertices);

                    for (unsigned i = 0; i < l_MeshData->mNumVertices; ++i)
                    {
                        Vertex it_Vertex{};
                        it_Vertex.Position = { l_MeshData->mVertices[i].x, l_MeshData->mVertices[i].y, l_MeshData->mVertices[i].z };
                        it_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                        if (l_MeshData->mTextureCoords[0])
                        {
                            it_Vertex.TexCoord = { l_MeshData->mTextureCoords[0][i].x, l_MeshData->mTextureCoords[0][i].y };
                        }

                        else
                        {
                            it_Vertex.TexCoord = { 0.0f, 0.0f };
                        }

                        l_Vertices.push_back(it_Vertex);
                    }

                    for (unsigned i = 0; i < l_MeshData->mNumFaces; ++i)
                    {
                        const aiFace& l_Face = l_MeshData->mFaces[i];
                        for (unsigned j = 0; j < l_Face.mNumIndices; ++j)
                        {
                            l_Indices.push_back(l_Face.mIndices[j]);
                        }
                    }
                }

                {
                    std::lock_guard<std::mutex> l_QueueLock(m_QueueMutex);
                    m_JobQueue.push([this, path, l_Vertices = std::move(l_Vertices), l_Indices = std::move(l_Indices), l_Promise]() mutable
                        {
                            auto a_Mesh = std::make_shared<Mesh>(m_Context);
                            if (auto a_Error = a_Mesh->Create(l_Vertices, l_Indices))
                            {
                                TR_CORE_ERROR("{}", *a_Error);
                                l_Promise->set_value(nullptr);
                                return;
                            }
                            {
                                std::lock_guard<std::mutex> l_CacheLock(m_CacheMutex);
                                m_MeshCache[path] = a_Mesh;
                            }
                            l_Promise->set_value(a_Mesh);
                        });
                }
            }).detach();

        return l_Future;
    }
}