#pragma once

#include <glm/glm.hpp>

namespace Trinity
{
    class Camera;

    namespace Culling
    {
        struct Frustum
        {
            glm::vec4 Planes[6];
        };

        Frustum CreateFrustum(const glm::mat4& viewProjection);
        bool IsVisible(const Frustum& frustum, const glm::vec3& position, float radius);
    }
}
