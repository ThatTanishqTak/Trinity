#pragma once

#include "ColliderDrawing.hpp"
#include "EditorCamera.hpp"
#include "EditorCamera3D.hpp"
#include "EditorGrid.hpp"
#include "EditorGrid3D.hpp"
#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_set>
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

    // Frames the selection, gliding there once the Scene panel is next drawn, as a double-click or F in the Hierarchy asks, and with follow keeps it framed as it moves
    void RequestFrame(bool follow) { m_FrameRequest = follow; }
    // The entities outlined beside the selection, as the Hierarchy's search matches, or null for none
    void SetHighlighted(std::function<const std::unordered_set<Trinity::UUID>*()> highlighted) { m_Highlighted = std::move(highlighted); }

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

    // Where a glide takes the camera in the mode it began in: the focus and distance in 3D, or the position, in X and Y, and height in 2D, and in 3D the yaw and pitch it turns to, when it turns
    struct Glide
    {
        glm::vec3 Focus{ 0.0f };
        float Distance = 1.0f;
        bool Mode3D = false;
        std::optional<glm::vec2> Angles;
    };

    void FollowPanelSize(std::uint32_t width, std::uint32_t height);
    void FollowScene();
    void SaveCamera();
    void MarkCameraChanged();
    void HandleInput(glm::vec2 imageMin, glm::vec2 viewportSize, bool hovered, bool focused);
    void HandleInput3D(glm::vec2 imageMin, glm::vec2 viewportSize, bool hovered, bool focused);
    void HandleGizmoKeys();
    void Frame(glm::vec2 viewportSize, bool follow);
    void UpdateGlide(glm::vec2 viewportSize);
    void StopGlide();
    void TurnView(float yaw, float pitch, bool orthographic, bool automatic);
    [[nodiscard]] EditorCamera GetFramed(glm::vec2 viewportSize);
    [[nodiscard]] EditorCamera3D GetFramed3D(glm::vec2 viewportSize);
    [[nodiscard]] Glide GetFramedGlide(glm::vec2 viewportSize);
    [[nodiscard]] Trinity::Entity PickLight(glm::vec2 mouse, glm::vec2 viewportSize);
    void RequestPick(glm::vec2 mouse, glm::vec2 viewportSize);
    void ApplyPick();
    [[nodiscard]] std::vector<std::uint32_t> GetOutlined();
    void AcceptAssetDrop(glm::vec2 imageMin, glm::vec2 viewportSize);
    void DrawOverlays(ImDrawList& drawList, glm::vec2 imageMin, glm::vec2 viewportSize);
    void DrawOverlays3D(ImDrawList& drawList, glm::vec2 imageMin, glm::vec2 viewportSize);
    void DrawStats(glm::vec2 viewportSize) const;
    void DrawGizmo(glm::vec2 imageMin, glm::vec2 viewportSize);
    void DrawOrientation(glm::vec2 imageMin, glm::vec2 viewportSize);
    [[nodiscard]] bool IsSnapping() const;
    void EndGizmoDrag();
    void DrawToolbar();

    [[nodiscard]] Trinity::TransformComponent ApplyGizmo(const glm::mat4& parentWorld, bool snap) const;
    [[nodiscard]] Trinity::TransformComponent ApplyGizmo3D(const glm::mat4& parentWorld) const;
    [[nodiscard]] glm::vec3 GetDropPoint(glm::vec2 imageMin, glm::vec2 viewportSize) const;

    [[nodiscard]] glm::vec2 GetViewportSize() const;

    Trinity::ImGuiLayer& m_ImGui;
    EditorSession& m_Session;
    std::uint64_t m_CloseListener = 0;
    ColliderDrawing m_Colliders;
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
    // Iso chosen by turning to a side view from perspective, which goes back to perspective once the view is turned by hand, as in Unity
    bool m_AutoOrthographic = false;
    bool m_OrientationHovered = false;
    // Framing's glide, eased towards each frame and dropped once there, or as the camera is moved by hand. The entity Shift+F follows, until the selection moves on or the camera is moved off it. A frame asked for from another panel, following or not
    std::optional<Glide> m_Glide;
    Trinity::UUID m_Followed;
    std::optional<bool> m_FrameRequest;
    std::function<const std::unordered_set<Trinity::UUID>*()> m_Highlighted;
    // A click's pixel, picked through the entity IDs the next frame draws, and how many picks are on their way back
    std::optional<glm::uvec2> m_PickPixel;
    std::uint32_t m_PicksPending = 0;
    bool m_LightHeatmap = false;
    // Every collider and joint in the scene, and while it plays, where its bodies touch, rather than the selection's alone
    bool m_PhysicsDebug = false;
    bool m_ShowStats = true;

    GizmoOperation m_GizmoOperation = GizmoOperation::Translate;
    bool m_GizmoLocal = true;
    // Every drag snaps while it is on, and Ctrl flips it for a drag, as in Godot and Blender
    bool m_Snap = false;
    glm::mat4 m_GizmoMatrix{ 1.0f };
    Trinity::TransformComponent m_GizmoStart;
    Trinity::UUID m_GizmoEntity;
    std::uint32_t m_GizmoID = 0;
    float m_ToolbarWidth = 0.0f;
    float m_ToolbarHeight = 0.0f;
    bool m_GizmoDragging = false;
    bool m_GizmoHovered = false;
};