#pragma once

#include <filesystem>

namespace Trinity
{
    class Scene;
    class AssetManager;

    class SceneSerializer
    {
    public:
        SceneSerializer(Scene* scene, AssetManager* assetManager = nullptr);

        void Serialize(const std::filesystem::path& path);
        bool Deserialize(const std::filesystem::path& path);

    private:
        Scene* m_Scene = nullptr;
        AssetManager* m_AssetManager = nullptr;
    };
}