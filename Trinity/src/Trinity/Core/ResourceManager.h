#pragma once

#include <string>
#include <memory>
#include <unordered_map>

#include "Trinity/Renderer/VertexBuffer.h"
#include "Trinity/Renderer/IndexBuffer.h"
#include "Trinity/Renderer/Texture.h"

namespace Trinity
{
    class VulkanContext;

    struct Mesh
    {
        Mesh(VulkanContext* context) : VertexBuffer(context), IndexBuffer(context) {}
        VertexBuffer VertexBuffer;
        IndexBuffer IndexBuffer;
    };

    class ResourceManager
    {
    public:
        explicit ResourceManager(VulkanContext* context);

        template<typename T>
        std::shared_ptr<T> Load(const std::string& path);

    private:
        VulkanContext* m_Context = nullptr;
        std::unordered_map<std::string, std::shared_ptr<Texture>> m_TextureCache;
        std::unordered_map<std::string, std::shared_ptr<Mesh>> m_MeshCache;
    };

    template<>
    std::shared_ptr<Texture> ResourceManager::Load<Texture>(const std::string& path);

    template<>
    std::shared_ptr<Mesh> ResourceManager::Load<Mesh>(const std::string& path);
}