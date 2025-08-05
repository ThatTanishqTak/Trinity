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

        auto a_Texture = std::make_shared<Texture>(m_Context);
        if (auto a_Error = a_Texture->LoadFromFile(path, 0, 0))
        {
            TR_CORE_ERROR("{}", *a_Error);
            
            return nullptr;
        }
        m_TextureCache[path] = a_Texture;

        return a_Texture;
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
        const aiScene* l_Scene = l_Importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);
        if (!l_Scene || !l_Scene->mRootNode || l_Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
        {
            TR_CORE_ERROR("Failed to load mesh: {}", path);
            
            return nullptr;
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

        auto a_Mesh = std::make_shared<Mesh>(m_Context);
        if (auto a_Error = a_Mesh->Create(l_Vertices, l_Indices))
        {
            TR_CORE_ERROR("{}", *a_Error);
            
            return nullptr;
        }
        m_MeshCache[path] = a_Mesh;

        return a_Mesh;
    }
}