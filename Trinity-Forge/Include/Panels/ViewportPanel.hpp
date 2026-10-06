#pragma once

#include "EditorCamera.hpp"
#include "EditorGrid.hpp"
#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <glm/glm.hpp>

#include <cstdint>

struct ImDrawList;

class ViewportPanel final : public Panel
{
public:
    ViewportPanel(Trinity::ImGuiLayer& imGui, EditorSession& session);
    ~ViewportPanel() override;

    void RenderScene(Trinity::RHI::CommandList& commands);

    [[nodiscard]] bool IsShowingStats() const { return m_ShowStats; }
    void SetShowingStats(bool show);

protected:
    void OnImGuiRender() override;

private:
    void FollowPanelSize(std::uint32_t width, std::uint32_t height);
    void FollowScene();
    void SaveCamera();
    void MarkCameraChanged();
    void HandleInput(glm::vec2 imageMin, glm::vec2 viewportSize, bool hovered, bool focused);
    void FrameSelection(glm::vec2 viewportSize);
    void AcceptTextureDrop(glm::vec2 imageMin, glm::vec2 viewportSize);
    void DrawOverlays(ImDrawList& drawList, glm::vec2 imageMin, glm::vec2 viewportSize);
    void DrawStats(glm::vec2 viewportSize) const;

    [[nodiscard]] glm::vec2 GetViewportSize() const;

    Trinity::ImGuiLayer& m_ImGui;
    EditorSession& m_Session;
    EditorCamera m_Camera;
    EditorGrid m_Grid;
    Trinity::UUID m_SceneID;
    std::uint32_t m_PanelWidth = 0;
    std::uint32_t m_PanelHeight = 0;
    double m_PanelSizeTime = 0.0;
    double m_CameraChangeTime = 0.0;
    bool m_CameraDirty = false;
    bool m_Panning = false;
    bool m_ShowStats = true;
};