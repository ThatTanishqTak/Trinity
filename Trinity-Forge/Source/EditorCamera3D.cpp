#include "EditorCamera3D.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    // Degrees turned per pixel the mouse moves, when looking and when orbiting
    constexpr float c_TurnPerPixel = 0.25f;
    // Short of straight up or down, where yaw would lose its meaning
    constexpr float c_MaximumPitch = 89.0f;
    // One wheel notch moves a tenth of the way to the focus, and changes the flying speed by a fifth
    constexpr float c_DollyStep = 0.9f;
    constexpr float c_SpeedStep = 1.2f;
    // Shift flies this many times faster
    constexpr float c_FastFactor = 4.0f;
    // Framing leaves a margin around the sphere
    constexpr float c_FrameMargin = 1.2f;
    // Far enough for the gizmo's projection, which needs a far plane, as the rendered one does not
    constexpr float c_GizmoFar = 100000.0f;

    float GetAspectRatio(glm::vec2 viewportSize)
    {
        return viewportSize.y > 0.0f ? viewportSize.x / viewportSize.y : 1.0f;
    }
}

void EditorCamera3D::Set(const glm::vec3& focus, float yaw, float pitch, float distance, float speed)
{
    m_Focus = focus;
    m_Yaw = std::remainder(yaw, 360.0f);
    m_Pitch = std::clamp(pitch, -c_MaximumPitch, c_MaximumPitch);
    m_Distance = std::clamp(distance, c_MinimumDistance, c_MaximumDistance);
    m_Speed = std::clamp(speed, c_MinimumSpeed, c_MaximumSpeed);
}

void EditorCamera3D::Reset()
{
    Set(glm::vec3(0.0f), c_DefaultYaw, c_DefaultPitch, c_DefaultDistance, c_DefaultSpeed);
}

// Turns about the eye, which stays where it is, so the focus moves round it
void EditorCamera3D::Look(glm::vec2 pixelDelta)
{
    const glm::vec3 l_Position = GetPosition();
    Set(m_Focus, m_Yaw + pixelDelta.x * c_TurnPerPixel, m_Pitch - pixelDelta.y * c_TurnPerPixel, m_Distance, m_Speed);
    m_Focus = l_Position + GetForward() * m_Distance;
}

// Turns about the focus, which stays where it is, so the eye moves round it
void EditorCamera3D::Orbit(glm::vec2 pixelDelta)
{
    Set(m_Focus, m_Yaw + pixelDelta.x * c_TurnPerPixel, m_Pitch - pixelDelta.y * c_TurnPerPixel, m_Distance, m_Speed);
}

// The point under the mouse at the focus' distance moves with it
void EditorCamera3D::Pan(glm::vec2 pixelDelta, glm::vec2 viewportSize)
{
    const float l_UnitsPerPixel = 2.0f * m_Distance * std::tan(glm::radians(c_FieldOfView) * 0.5f) / std::max(viewportSize.y, 1.0f);
    m_Focus += (GetUp() * pixelDelta.y - GetRight() * pixelDelta.x) * l_UnitsPerPixel;
}

// Positive steps move towards the focus. Once the eye is as close as it may come, the focus is pushed on ahead of it, so the wheel never stops moving the camera
void EditorCamera3D::Dolly(float steps)
{
    const float l_Distance = m_Distance * std::pow(c_DollyStep, steps);
    if (l_Distance < c_MinimumDistance)
    {
        m_Focus += GetForward() * (m_Distance * (1.0f - c_DollyStep) * steps);

        return;
    }

    m_Distance = std::min(l_Distance, c_MaximumDistance);
}

// The direction is right, world up and forward, each from -1 to 1. The eye and focus move together
void EditorCamera3D::Fly(const glm::vec3& direction, float seconds, bool fast)
{
    const glm::vec3 l_Move = GetRight() * direction.x + glm::vec3(0.0f, direction.y, 0.0f) + GetForward() * direction.z;
    const float l_Length = glm::length(l_Move);
    if (l_Length < 1e-6f)
    {
        return;
    }

    m_Focus += l_Move / std::max(l_Length, 1.0f) * m_Speed * (fast ? c_FastFactor : 1.0f) * seconds;
}

void EditorCamera3D::ChangeSpeed(float steps)
{
    m_Speed = std::clamp(m_Speed * std::pow(c_SpeedStep, steps), c_MinimumSpeed, c_MaximumSpeed);
}

// Looking at the centre from the way the camera already looks, from where the sphere fits the narrower of the view's angles
void EditorCamera3D::Frame(const glm::vec3& center, float radius, glm::vec2 viewportSize)
{
    const float l_HalfVertical = glm::radians(c_FieldOfView) * 0.5f;
    const float l_HalfHorizontal = std::atan(std::tan(l_HalfVertical) * GetAspectRatio(viewportSize));
    const float l_Radius = std::max(radius, 0.1f) * c_FrameMargin;

    Set(center, m_Yaw, m_Pitch, l_Radius / std::sin(std::min(l_HalfVertical, l_HalfHorizontal)), m_Speed);
}

glm::vec3 EditorCamera3D::GetPosition() const
{
    return m_Focus - GetForward() * m_Distance;
}

glm::vec3 EditorCamera3D::GetForward() const
{
    const float l_Yaw = glm::radians(m_Yaw);
    const float l_Pitch = glm::radians(m_Pitch);

    return { std::sin(l_Yaw) * std::cos(l_Pitch), std::sin(l_Pitch), -std::cos(l_Yaw) * std::cos(l_Pitch) };
}

glm::vec3 EditorCamera3D::GetRight() const
{
    const float l_Yaw = glm::radians(m_Yaw);

    return { std::cos(l_Yaw), 0.0f, std::sin(l_Yaw) };
}

glm::vec3 EditorCamera3D::GetUp() const
{
    return glm::cross(GetRight(), GetForward());
}

glm::mat4 EditorCamera3D::GetView() const
{
    return glm::lookAt(GetPosition(), m_Focus, glm::vec3(0.0f, 1.0f, 0.0f));
}

// Reversed depth with no far plane, as a perspective CameraComponent has it
glm::mat4 EditorCamera3D::GetProjection(glm::vec2 viewportSize) const
{
    const float l_Focal = 1.0f / std::tan(glm::radians(c_FieldOfView) * 0.5f);
    glm::mat4 l_Projection(0.0f);
    l_Projection[0][0] = l_Focal / GetAspectRatio(viewportSize);
    l_Projection[1][1] = l_Focal;
    l_Projection[2][3] = -1.0f;
    l_Projection[3][2] = c_Near;

    return l_Projection;
}

// With depth the usual way round and a far plane, as ImGuizmo takes it
glm::mat4 EditorCamera3D::GetGizmoProjection(glm::vec2 viewportSize) const
{
    return glm::perspective(glm::radians(c_FieldOfView), GetAspectRatio(viewportSize), c_Near, c_GizmoFar);
}

// The unit direction from the eye through a pixel, counted from the image's top-left corner with Y down
glm::vec3 EditorCamera3D::GetRayDirection(glm::vec2 pixel, glm::vec2 viewportSize) const
{
    const float l_TanHalf = std::tan(glm::radians(c_FieldOfView) * 0.5f);
    const float l_X = (pixel.x / std::max(viewportSize.x, 1.0f) * 2.0f - 1.0f) * l_TanHalf * GetAspectRatio(viewportSize);
    const float l_Y = (1.0f - pixel.y / std::max(viewportSize.y, 1.0f) * 2.0f) * l_TanHalf;

    return glm::normalize(GetForward() + GetRight() * l_X + GetUp() * l_Y);
}