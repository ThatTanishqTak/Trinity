#pragma once

#include <glm/glm.hpp>

#include "Trinity/Renderer/VertexBuffer.h"
#include "Trinity/Renderer/IndexBuffer.h"
#include "Trinity/Renderer/Texture.h"

namespace Trinity
{
    struct Transform
    {
        glm::vec3 Translation{ 0.0f };
        glm::vec3 Rotation{ 0.0f };
        glm::vec3 Scale{ 1.0f };

        glm::mat4 GetTransform() const;
    };

    struct MeshRenderer
    {
        VertexBuffer* MeshVertexBuffer = nullptr;
        IndexBuffer* MeshIndexBuffer = nullptr;
        Texture* MeshTexture = nullptr;
    };

    struct Light
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Color{ 1.0f };
    };

    struct Material
    {
        glm::vec3 Albedo{ 1.0f };
        float Roughness{ 1.0f };
    };
}