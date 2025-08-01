#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Trinity
{
    class Camera
    {
    public:
        Camera() = default;
        ~Camera() = default;

        void SetPosition(const glm::vec3& a_Position) { m_Position = a_Position; }
        void SetRotation(const glm::vec3& a_Rotation) { m_Rotation = a_Rotation; }
        void SetProjection(float a_Fov, float a_AspectRatio, float a_Near, float a_Far);

        glm::mat4 GetViewMatrix() const;
        glm::mat4 GetProjectionMatrix() const;

    private:
        glm::vec3 m_Position{ 0.0f, 0.0f, 3.0f };
        glm::vec3 m_Rotation{ 0.0f };
        float m_Fov = 45.0f;
        float m_AspectRatio = 1.0f;
        float m_Near = 0.1f;
        float m_Far = 100.0f;
    };
}