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

        Frustum CreateFrustum(const glm::mat4& a_VP);
        bool IsVisible(const Frustum& a_Frustum, const glm::vec3& a_Position, float a_Radius);
    }
}
