#pragma once

#include <filesystem>

namespace Trinity
{
    class Scene;
    class ResourceManager;

    class SceneSerializer
    {
    public:
        SceneSerializer(Scene* scene, ResourceManager* resourceManager = nullptr);

        void Serialize(const std::filesystem::path& path);
        bool Deserialize(const std::filesystem::path& path);

    private:
        Scene* m_Scene = nullptr;
        ResourceManager* m_ResourceManager = nullptr;
    };
}