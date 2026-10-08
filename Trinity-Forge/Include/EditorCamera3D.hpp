#pragma once

#include <glm/glm.hpp>

// The Viewport's perspective camera in 3D: an eye looking at a focus point from a distance, turned by yaw about +Y and pitch about its right, both in degrees. At yaw and pitch 0 it looks down -Z as cameras do, positive yaw turns it to the right and positive pitch up
class EditorCamera3D
{
public:
    static constexpr float c_FieldOfView = 60.0f;
    static constexpr float c_Near = 0.05f;
    static constexpr float c_DefaultYaw = 30.0f;
    static constexpr float c_DefaultPitch = -25.0f;
    static constexpr float c_DefaultDistance = 10.0f;
    static constexpr float c_DefaultSpeed = 5.0f;
    static constexpr float c_MinimumDistance = 0.05f;
    static constexpr float c_MaximumDistance = 100000.0f;
    static constexpr float c_MinimumSpeed = 0.01f;
    static constexpr float c_MaximumSpeed = 1000.0f;

    [[nodiscard]] glm::vec3 GetFocus() const { return m_Focus; }
    [[nodiscard]] float GetYaw() const { return m_Yaw; }
    [[nodiscard]] float GetPitch() const { return m_Pitch; }
    [[nodiscard]] float GetDistance() const { return m_Distance; }
    [[nodiscard]] float GetSpeed() const { return m_Speed; }

    void Set(const glm::vec3& focus, float yaw, float pitch, float distance, float speed);
    void Reset();

    void Look(glm::vec2 pixelDelta);
    void Orbit(glm::vec2 pixelDelta);
    void Pan(glm::vec2 pixelDelta, glm::vec2 viewportSize);
    void Dolly(float steps);
    void Fly(const glm::vec3& direction, float seconds, bool fast);
    void ChangeSpeed(float steps);
    void Frame(const glm::vec3& center, float radius, glm::vec2 viewportSize);

    [[nodiscard]] glm::vec3 GetPosition() const;
    [[nodiscard]] glm::vec3 GetForward() const;
    [[nodiscard]] glm::vec3 GetRight() const;
    [[nodiscard]] glm::vec3 GetUp() const;
    [[nodiscard]] glm::mat4 GetView() const;
    [[nodiscard]] glm::mat4 GetProjection(glm::vec2 viewportSize) const;
    [[nodiscard]] glm::mat4 GetGizmoProjection(glm::vec2 viewportSize) const;
    [[nodiscard]] glm::vec3 GetRayDirection(glm::vec2 pixel, glm::vec2 viewportSize) const;

private:
    glm::vec3 m_Focus{ 0.0f };
    float m_Yaw = c_DefaultYaw;
    float m_Pitch = c_DefaultPitch;
    float m_Distance = c_DefaultDistance;
    float m_Speed = c_DefaultSpeed;
};