#include "Trinity/trpch.h"

#include "Trinity/Camera/Culling.h"
#include "Trinity/Camera/Camera.h"

#include <glm/gtc/matrix_access.hpp>
#include <algorithm>

namespace Trinity
{
    namespace Culling
    {
        Frustum CreateFrustum(const glm::mat4& a_VP)
        {
            Frustum l_Frustum{};
            glm::vec4 l_RowX = glm::row(a_VP, 0);
            glm::vec4 l_RowY = glm::row(a_VP, 1);
            glm::vec4 l_RowZ = glm::row(a_VP, 2);
            glm::vec4 l_RowW = glm::row(a_VP, 3);

            l_Frustum.Planes[0] = l_RowW + l_RowX; // Left
            l_Frustum.Planes[1] = l_RowW - l_RowX; // Right
            l_Frustum.Planes[2] = l_RowW + l_RowY; // Bottom
            l_Frustum.Planes[3] = l_RowW - l_RowY; // Top
            l_Frustum.Planes[4] = l_RowW + l_RowZ; // Near
            l_Frustum.Planes[5] = l_RowW - l_RowZ; // Far

            for (auto& l_Plane : l_Frustum.Planes)
            {
                float l_Length = glm::length(glm::vec3(l_Plane));
                l_Plane /= l_Length;
            }

            return l_Frustum;
        }

        bool IsVisible(const Frustum& a_Frustum, const glm::vec3& a_Position, float a_Radius)
        {
            for (const auto& l_Plane : a_Frustum.Planes)
            {
                float l_Distance = glm::dot(glm::vec3(l_Plane), a_Position) + l_Plane.w;
                if (l_Distance < -a_Radius)
                {
                    return false;
                }
            }
            return true;
        }
    }
}