#include "Trinity/trpch.h"

#include "Trinity/Core/ResourceManager.h"

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

        auto mesh = std::make_shared<Mesh>(m_Context);
        // Mesh loading from file would occur here
        m_MeshCache[path] = mesh;
        return mesh;
    }
}