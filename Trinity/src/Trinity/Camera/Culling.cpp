#include "Trinity/trpch.h"

#include "Trinity/Camera/Culling.h"
#include "Trinity/Camera/Camera.h"

#include <glm/gtc/matrix_access.hpp>
#include <algorithm>

namespace Trinity
{
    namespace Culling
    {
        Frustum CreateFrustum(const glm::mat4& viewProjection)
        {
            Frustum l_Frustum{};

            glm::vec4 l_ColumnX = glm::column(viewProjection, 0);
            glm::vec4 l_ColumnY = glm::column(viewProjection, 1);
            glm::vec4 l_ColumnZ = glm::column(viewProjection, 2);
            glm::vec4 l_ColumnW = glm::column(viewProjection, 3);

            l_Frustum.Planes[0] = l_ColumnW + l_ColumnX; // Left
            l_Frustum.Planes[1] = l_ColumnW - l_ColumnX; // Right
            l_Frustum.Planes[2] = l_ColumnW + l_ColumnY; // Bottom
            l_Frustum.Planes[3] = l_ColumnW - l_ColumnY; // Top
            l_Frustum.Planes[4] = l_ColumnW + l_ColumnZ; // Near
            l_Frustum.Planes[5] = l_ColumnW - l_ColumnZ; // Far

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