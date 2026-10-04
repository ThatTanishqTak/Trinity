#pragma once

#include "Panels/Panel.hpp"
#include "SceneLayer.hpp"

#include <Trinity.hpp>

#include <cstdint>

// The scene target, kept the size of the panel in pixels, with a stats overlay. The scene gets the mouse while the panel is hovered and the keyboard while it has focus
class ViewportPanel final : public Panel
{
public:
    ViewportPanel(Trinity::ImGuiLayer& imGui, const SceneLayer& scene);

    [[nodiscard]] bool IsShowingStats() const { return m_ShowStats; }
    void SetShowingStats(bool show);

protected:
    void OnImGuiRender() override;

private:
    void FollowPanelSize(std::uint32_t width, std::uint32_t height);
    void DrawStats(bool mouse, bool keyboard) const;

    Trinity::ImGuiLayer& m_ImGui;
    const SceneLayer& m_Scene;
    std::uint32_t m_PanelWidth = 0;
    std::uint32_t m_PanelHeight = 0;
    double m_PanelSizeTime = 0.0;
    bool m_ShowStats = true;
};