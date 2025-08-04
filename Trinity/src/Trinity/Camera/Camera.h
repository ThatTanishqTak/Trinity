#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#undef near
#undef far

namespace Trinity
{
    class Camera
    {
    public:
        enum class ProjectionType
        {
            Perspective = 0,
            Orthographic = 1
        };

        Camera() = default;
        ~Camera() = default;

        void SetPosition(const glm::vec3& position) { m_Position = position; }
        void SetRotation(const glm::vec3& rotation) { m_Rotation = rotation; }

        void SetPerspective(float fov, float aspectRatio, float near, float far);
        void SetOrthographic(float size, float aspectRatio, float near, float far);

        ProjectionType GetProjectionType() const { return m_ProjectionType; }
        float GetFov() const { return m_Fov; }
        float GetOrthoSize() const { return m_Size; }
        float GetAspectRatio() const { return m_AspectRatio; }
        float GetNear() const { return m_Near; }
        float GetFar() const { return m_Far; }

        glm::mat4 GetViewMatrix() const;
        glm::mat4 GetProjectionMatrix() const;

    private:
        glm::vec3 m_Position{ 0.0f, 0.0f, 3.0f };
        glm::vec3 m_Rotation{ 0.0f };

        float m_Fov = 45.0f;
        float m_Size = 10.0f;
        float m_AspectRatio = 1.0f;
        float m_Near = 0.1f;
        float m_Far = 100.0f;
        
        ProjectionType m_ProjectionType = ProjectionType::Perspective;
    };
}