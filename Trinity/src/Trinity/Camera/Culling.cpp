#include "Trinity/trpch.h"

#include "Trinity/Camera/Culling.h"
#include "Trinity/Camera/Camera.h"

#include <glm/gtc/matrix_access.hpp>
#include <algorithm>

namespace Trinity
{
    namespace Culling
    {
        Frustum CreateFrustum(const glm::mat4& viewpoint)
        {
            Frustum l_Frustum{};
            glm::vec4 l_RowX = glm::row(viewpoint, 0);
            glm::vec4 l_RowY = glm::row(viewpoint, 1);
            glm::vec4 l_RowZ = glm::row(viewpoint, 2);
            glm::vec4 l_RowW = glm::row(viewpoint, 3);

            l_Frustum.Planes[0] = l_RowW + l_RowX; // Left
            l_Frustum.Planes[1] = l_RowW - l_RowX; // Right
            l_Frustum.Planes[2] = l_RowW + l_RowY; // Bottom
            l_Frustum.Planes[3] = l_RowW - l_RowY; // Top
            l_Frustum.Planes[4] = l_RowW + l_RowZ; // Near
            l_Frustum.Planes[5] = l_RowW - l_RowZ; // Far

            for (auto& it_Plane : l_Frustum.Planes)
            {
                float l_Length = glm::length(glm::vec3(it_Plane));
                it_Plane /= l_Length;
            }

            return l_Frustum;
        }

        bool IsVisible(const Frustum& frustum, const glm::vec3& position, float radius)
        {
            for (const auto& it_Plane : frustum.Planes)
            {
                float l_Distance = glm::dot(glm::vec3(it_Plane), position) + it_Plane.w;
                if (l_Distance < -radius)
                {
                    return false;
                }
            }

            return true;
        }
    }
}