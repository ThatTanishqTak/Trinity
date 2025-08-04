#include "Trinity/trpch.h"

#include "Trinity/Camera/Camera.h"

namespace Trinity
{
    void Camera::SetPerspective(float fov, float aspectRatio, float near, float far)
    {
        m_ProjectionType = ProjectionType::Perspective;
        m_Fov = fov;
        m_AspectRatio = aspectRatio;
        m_Near = near;
        m_Far = far;
    }

    void Camera::SetOrthographic(float size, float aspectRatio, float near, float far)
    {
        m_ProjectionType = ProjectionType::Orthographic;
        m_Size = size;
        m_AspectRatio = aspectRatio;
        m_Near = near;
        m_Far = far;
    }

    glm::mat4 Camera::GetViewMatrix() const
    {
        glm::mat4 l_View = glm::mat4(1.0f);
        l_View = glm::rotate(l_View, glm::radians(m_Rotation.x), { 1.0f, 0.0f, 0.0f });
        l_View = glm::rotate(l_View, glm::radians(m_Rotation.y), { 0.0f, 1.0f, 0.0f });
        l_View = glm::rotate(l_View, glm::radians(m_Rotation.z), { 0.0f, 0.0f, 1.0f });
        l_View = glm::translate(l_View, -m_Position);

        return l_View;
    }

    glm::mat4 Camera::GetProjectionMatrix() const
    {
        glm::mat4 l_Proj{};
        if (m_ProjectionType == ProjectionType::Perspective)
        {
            l_Proj = glm::perspective(glm::radians(m_Fov), m_AspectRatio, m_Near, m_Far);
        }

        else
        {
            l_Proj = glm::ortho(-m_Size * m_AspectRatio, m_Size * m_AspectRatio, -m_Size, m_Size, m_Near, m_Far);
        }

        l_Proj[1][1] *= -1.0f;

        return l_Proj;
    }
}