#pragma once

#include <glm/glm.hpp>

class EditorCamera
{
public:
    static constexpr float c_DefaultHeight = 10.0f;
    static constexpr float c_MinimumHeight = 0.01f;
    static constexpr float c_MaximumHeight = 100000.0f;

    [[nodiscard]] glm::vec2 GetPosition() const { return m_Position; }
    [[nodiscard]] float GetHeight() const { return m_Height; }

    void Set(glm::vec2 position, float height);
    void Reset();

    void Pan(glm::vec2 pixelDelta, glm::vec2 viewportSize);
    void ZoomAt(glm::vec2 pixel, glm::vec2 viewportSize, float steps);
    void Frame(glm::vec2 minimum, glm::vec2 maximum, glm::vec2 viewportSize);

    [[nodiscard]] glm::vec2 GetHalfExtent(glm::vec2 viewportSize) const;
    [[nodiscard]] glm::vec2 ScreenToWorld(glm::vec2 pixel, glm::vec2 viewportSize) const;
    [[nodiscard]] glm::vec2 WorldToScreen(glm::vec2 world, glm::vec2 viewportSize) const;
    [[nodiscard]] glm::mat4 GetViewProjection(glm::vec2 viewportSize) const;

private:
    glm::vec2 m_Position{ 0.0f };
    float m_Height = c_DefaultHeight;
};