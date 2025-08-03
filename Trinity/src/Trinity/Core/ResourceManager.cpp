#include "Trinity/trpch.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "Trinity/Core/ResourceManager.h"

#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    ResourceManager::ResourceManager(VulkanContext* context) : m_Context(context)
    {
    }

    template<>
    std::shared_ptr<Texture> ResourceManager::Load<Texture>(const std::string& path)
    {
        auto it = m_TextureCache.find(path);
        if (it != m_TextureCache.end())
        {
            return it->second;
        }

        auto texture = std::make_shared<Texture>(m_Context);
        texture->LoadFromFile(path, 0, 0);
        m_TextureCache[path] = texture;
        return texture;
    }

    template<>
    std::shared_ptr<Mesh> ResourceManager::Load<Mesh>(const std::string& path)
    {
        auto it = m_MeshCache.find(path);
        if (it != m_MeshCache.end())
        {
            return it->second;
        }

        Assimp::Importer l_Importer;
        const aiScene* scene = l_Importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);
        if (!scene || !scene->mRootNode || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
        {
            TR_CORE_ERROR("Failed to load mesh: {}", path);
            
            return nullptr;
        }

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        if (scene->mNumMeshes > 0)
        {
            aiMesh* meshData = scene->mMeshes[0];
            vertices.reserve(meshData->mNumVertices);

            for (unsigned i = 0; i < meshData->mNumVertices; ++i)
            {
                Vertex v{};
                v.Position = { meshData->mVertices[i].x, meshData->mVertices[i].y, meshData->mVertices[i].z };
                v.Color = { 1.0f, 1.0f, 1.0f };
                if (meshData->mTextureCoords[0])
                {
                    v.TexCoord = { meshData->mTextureCoords[0][i].x, meshData->mTextureCoords[0][i].y };
                }
                
                else
                {
                    v.TexCoord = { 0.0f, 0.0f };
                }
                vertices.push_back(v);
            }

            for (unsigned i = 0; i < meshData->mNumFaces; ++i)
            {
                const aiFace& face = meshData->mFaces[i];
                for (unsigned j = 0; j < face.mNumIndices; ++j)
                {
                    indices.push_back(face.mIndices[j]);
                }
            }
        }

        auto mesh = std::make_shared<Mesh>(m_Context);
        if (!mesh->Create(vertices, indices))
        {
            return nullptr;
        }
        m_MeshCache[path] = mesh;

        return mesh;
    }
}