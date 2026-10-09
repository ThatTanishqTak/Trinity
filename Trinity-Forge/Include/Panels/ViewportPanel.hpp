#pragma once

#include "EditorCamera.hpp"
#include "EditorCamera3D.hpp"
#include "EditorGrid.hpp"
#include "EditorGrid3D.hpp"
#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

struct ImDrawList;
struct ImGuiTextBuffer;

class ViewportPanel final : public Panel
{
public:
    ViewportPanel(Trinity::ImGuiLayer& imGui, EditorSession& session);
    ~ViewportPanel() override;

    void PrepareScene();
    void RenderScene(Trinity::RHI::CommandList& commands);

    [[nodiscard]] bool IsShowingStats() const { return m_ShowStats; }
    void SetShowingStats(bool show);

    [[nodiscard]] bool ReadSetting(std::string_view key, std::string_view value);
    void WriteSettings(ImGuiTextBuffer& buffer) const;

protected:
    void OnImGuiRender() override;

private:
    enum class GizmoOperation : std::uint8_t
    {
        Translate,
        Rotate,
        Scale
    };

    void FollowPanelSize(std::uint32_t width, std::uint32_t height);
    void FollowScene();
    void SaveCamera();
    void MarkCameraChanged();
    void HandleInput(glm::vec2 imageMin, glm::vec2 viewportSize, bool hovered, bool focused);
    void HandleInput3D(glm::vec2 imageMin, glm::vec2 viewportSize, bool hovered, bool focused);
    void HandleGizmoKeys();
    void FrameSelection(glm::vec2 viewportSize);
    void FrameSelection3D(glm::vec2 viewportSize);
    [[nodiscard]] Trinity::Entity PickLight(glm::vec2 mouse, glm::vec2 viewportSize);
    void RequestPick(glm::vec2 mouse, glm::vec2 viewportSize);
    void ApplyPick();
    [[nodiscard]] std::vector<std::uint32_t> GetOutlined();
    void AcceptAssetDrop(glm::vec2 imageMin, glm::vec2 viewportSize);
    void DrawOverlays(ImDrawList& drawList, glm::vec2 imageMin, glm::vec2 viewportSize);
    void DrawOverlays3D(ImDrawList& drawList, glm::vec2 imageMin, glm::vec2 viewportSize);
    void DrawStats(glm::vec2 viewportSize) const;
    void DrawGizmo(glm::vec2 imageMin, glm::vec2 viewportSize);
    void EndGizmoDrag();
    void DrawToolbar();

    [[nodiscard]] Trinity::TransformComponent ApplyGizmo(const glm::mat4& parentWorld, bool snap) const;
    [[nodiscard]] Trinity::TransformComponent ApplyGizmo3D(const glm::mat4& parentWorld) const;
    [[nodiscard]] glm::vec3 GetDropPoint(glm::vec2 imageMin, glm::vec2 viewportSize) const;

    [[nodiscard]] glm::vec2 GetViewportSize() const;

    Trinity::ImGuiLayer& m_ImGui;
    EditorSession& m_Session;
    EditorCamera m_Camera;
    EditorGrid m_Grid;
    EditorCamera3D m_Camera3D;
    EditorGrid3D m_Grid3D;
    Trinity::UUID m_SceneID;
    std::uint32_t m_PanelWidth = 0;
    std::uint32_t m_PanelHeight = 0;
    double m_PanelSizeTime = 0.0;
    double m_CameraChangeTime = 0.0;
    bool m_CameraDirty = false;
    bool m_Panning = false;
    // The editor camera looks down -Z at the sprites in 2D, and flies and orbits in perspective in 3D. Each scene keeps its own choice with its camera
    bool m_Mode3D = false;
    bool m_Flying = false;
    bool m_Orbiting = false;
    // A click's pixel, picked through the entity IDs the next frame draws, and how many picks are on their way back
    std::optional<glm::uvec2> m_PickPixel;
    std::uint32_t m_PicksPending = 0;
    bool m_LightHeatmap = false;
    bool m_ShowStats = true;

    GizmoOperation m_GizmoOperation = GizmoOperation::Translate;
    bool m_GizmoLocal = true;
    glm::vec3 m_SnapSteps{ 0.5f, 15.0f, 0.1f };
    glm::mat4 m_GizmoMatrix{ 1.0f };
    Trinity::TransformComponent m_GizmoStart;
    Trinity::UUID m_GizmoEntity;
    std::uint32_t m_GizmoID = 0;
    float m_ToolbarWidth = 0.0f;
    bool m_GizmoDragging = false;
    bool m_GizmoHovered = false;
};