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
}