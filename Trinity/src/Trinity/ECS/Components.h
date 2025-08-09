#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Trinity
{
    struct TransformComponent
    {
        glm::vec3 Translation{ 0.0f };
        glm::vec3 Rotation{ 0.0f };
        glm::vec3 Scale{ 1.0f };

        glm::mat4 GetTransform() const;
    };

    class Mesh;
    class Texture;

    struct MeshComponent
    {
        std::string MeshPath;
        std::string TexturePath;
        std::shared_ptr<Mesh> MeshHandle = nullptr;
        std::shared_ptr<Texture> MeshTexture = nullptr;
    };

    struct LightComponent
    {
        enum class Type
        {
            Directional = 0,
            Point = 1,
            Spot = 2
        };

        glm::vec3 Position{ 0.0f };
        glm::vec3 Color{ 1.0f };
        float Intensity{ 1.0f };
        Type LightType{ Type::Point };
    };

    struct MaterialComponent
    {
        glm::vec3 Albedo{ 1.0f };
        float Roughness{ 1.0f };
        float Metallic{ 0.0f };
        float Specular{ 0.5f };

        std::string NormalMapPath;
        std::string RoughnessMapPath;
        std::string MetallicMapPath;
        std::shared_ptr<Texture> NormalMap = nullptr;
        std::shared_ptr<Texture> RoughnessMap = nullptr;
        std::shared_ptr<Texture> MetallicMap = nullptr;
    };
}