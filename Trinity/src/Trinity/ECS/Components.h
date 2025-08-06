#pragma once

#include <glm/glm.hpp>
#include <memory>

namespace Trinity
{
    struct Transform
    {
        glm::vec3 Translation{ 0.0f };
        glm::vec3 Rotation{ 0.0f };
        glm::vec3 Scale{ 1.0f };

        glm::mat4 GetTransform() const;
    };

    class Mesh;
    class Texture;

    struct MeshRenderer
    {
        std::shared_ptr<Mesh> Mesh = nullptr;
        std::shared_ptr<Texture> MeshTexture = nullptr;
    };

    struct Light
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

    struct Material
    {
        glm::vec3 Albedo{ 1.0f };
        float Roughness{ 1.0f };
        float Metallic{ 0.0f };
        float Specular{ 0.5f };
    };
}