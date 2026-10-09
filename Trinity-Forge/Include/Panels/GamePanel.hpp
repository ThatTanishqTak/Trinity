#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>

struct ImGuiTextBuffer;

// The scene through its primary camera, as the game shows it, in a view of the Renderer's own. Free fills the panel, an aspect ratio fills as much of it as that shape allows, and a fixed resolution is drawn at that size and shown at 1:1, or scaled down to fit a smaller panel, letterboxed
class GamePanel final : public Panel
{
public:
    explicit GamePanel(EditorSession& session);
    ~GamePanel() override;

    // Submits the scene to the panel's view, while the panel is shown
    void PrepareScene();

    [[nodiscard]] bool ReadSetting(std::string_view key, std::string_view value);
    void WriteSettings(ImGuiTextBuffer& buffer) const;

protected:
    void OnImGuiRender() override;

private:
    enum class Aspect : std::uint8_t
    {
        Free,
        Ratio16x9,
        Ratio16x10,
        Ratio4x3,
        Fixed1280x720,
        Fixed1920x1080,
        Fixed2560x1440,
        Fixed3840x2160,

        Count
    };

    void DrawToolbar();
    void FollowArea(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] glm::uvec2 GetWantedSize() const;

    EditorSession& m_Session;
    Trinity::ViewID m_View = Trinity::Renderer::c_MainView;
    Aspect m_Aspect = Aspect::Free;
    // The space under the toolbar, and when it last changed, so a size is taken once it settles
    std::uint32_t m_AreaWidth = 1;
    std::uint32_t m_AreaHeight = 1;
    double m_AreaTime = 0.0;
    // Whether the panel was drawn this frame, which only then submits the scene
    bool m_Shown = false;
};