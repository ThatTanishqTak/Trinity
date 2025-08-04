#include "Trinity/trpch.h"

#include "Trinity/Camera/Camera.h"

namespace Trinity
{
    void Camera::SetPerspective(float a_Fov, float a_AspectRatio, float a_Near, float a_Far)
    {
        m_ProjectionType = ProjectionType::Perspective;
        m_Fov = a_Fov;
        m_AspectRatio = a_AspectRatio;
        m_Near = a_Near;
        m_Far = a_Far;
    }

    void Camera::SetOrthographic(float a_Size, float a_AspectRatio, float a_Near, float a_Far)
    {
        m_ProjectionType = ProjectionType::Orthographic;
        m_Size = a_Size;
        m_AspectRatio = a_AspectRatio;
        m_Near = a_Near;
        m_Far = a_Far;
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