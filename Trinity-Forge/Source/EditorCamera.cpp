#include "EditorCamera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    // One wheel notch zooms by a tenth
    constexpr float c_ZoomStep = 1.1f;

    // Framing leaves a margin, and a single small sprite is not blown up to fill the view
    constexpr float c_FrameMargin = 1.2f;
    constexpr float c_MinimumFrameHeight = 1.0f;

    // Sprites sit near Z = 0, and the editor shows any Z the scene uses, with reversed depth as CameraComponent has it
    constexpr float c_DepthRange = 10000.0f;

    float GetAspectRatio(glm::vec2 viewportSize)
    {
        return viewportSize.y > 0.0f ? viewportSize.x / viewportSize.y : 1.0f;
    }

    // Pixels count from the image's top-left corner, with Y down
    glm::vec2 ToNormalized(glm::vec2 pixel, glm::vec2 viewportSize)
    {
        return { pixel.x / std::max(viewportSize.x, 1.0f) * 2.0f - 1.0f, 1.0f - pixel.y / std::max(viewportSize.y, 1.0f) * 2.0f };
    }
}

void EditorCamera::Set(glm::vec2 position, float height)
{
    m_Position = position;
    m_Height = std::clamp(height, c_MinimumHeight, c_MaximumHeight);
}

void EditorCamera::Reset()
{
    Set(glm::vec2(0.0f), c_DefaultHeight);
}

// The world moves with the mouse
void EditorCamera::Pan(glm::vec2 pixelDelta, glm::vec2 viewportSize)
{
    const glm::vec2 l_HalfExtent = GetHalfExtent(viewportSize);
    m_Position -= glm::vec2(pixelDelta.x / std::max(viewportSize.x, 1.0f) * 2.0f * l_HalfExtent.x, -pixelDelta.y / std::max(viewportSize.y, 1.0f) * 2.0f * l_HalfExtent.y);
}

// The world point under the pixel stays under it, so positive steps zoom in towards the cursor
void EditorCamera::ZoomAt(glm::vec2 pixel, glm::vec2 viewportSize, float steps)
{
    const glm::vec2 l_Anchor = ScreenToWorld(pixel, viewportSize);
    m_Height = std::clamp(m_Height * std::pow(c_ZoomStep, -steps), c_MinimumHeight, c_MaximumHeight);
    m_Position = l_Anchor - ToNormalized(pixel, viewportSize) * GetHalfExtent(viewportSize);
}

// Centred on the box, which fits with a margin whichever of its sides is the tighter fit
void EditorCamera::Frame(glm::vec2 minimum, glm::vec2 maximum, glm::vec2 viewportSize)
{
    const glm::vec2 l_Size = maximum - minimum;
    const float l_Height = std::max(l_Size.y, l_Size.x / GetAspectRatio(viewportSize)) * c_FrameMargin;
    Set((minimum + maximum) * 0.5f, std::max(l_Height, c_MinimumFrameHeight));
}

glm::vec2 EditorCamera::GetHalfExtent(glm::vec2 viewportSize) const
{
    return { m_Height * 0.5f * GetAspectRatio(viewportSize), m_Height * 0.5f };
}

glm::vec2 EditorCamera::ScreenToWorld(glm::vec2 pixel, glm::vec2 viewportSize) const
{
    return m_Position + ToNormalized(pixel, viewportSize) * GetHalfExtent(viewportSize);
}

glm::vec2 EditorCamera::WorldToScreen(glm::vec2 world, glm::vec2 viewportSize) const
{
    const glm::vec2 l_Normalized = (world - m_Position) / GetHalfExtent(viewportSize);

    return { (l_Normalized.x + 1.0f) * 0.5f * viewportSize.x, (1.0f - l_Normalized.y) * 0.5f * viewportSize.y };
}

glm::mat4 EditorCamera::GetViewProjection(glm::vec2 viewportSize) const
{
    const glm::vec2 l_HalfExtent = GetHalfExtent(viewportSize);

    return glm::ortho(m_Position.x - l_HalfExtent.x, m_Position.x + l_HalfExtent.x, m_Position.y - l_HalfExtent.y, m_Position.y + l_HalfExtent.y, c_DepthRange, -c_DepthRange);
}