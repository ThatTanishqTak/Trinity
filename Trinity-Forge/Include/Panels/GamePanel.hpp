#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

struct ImGuiTextBuffer;
struct ImVec2;

// The scene through its primary camera, as the game shows it, in a view of the Renderer's own, under a toolbar as Unity's Game view has it. Free fills the panel, an aspect ratio fills as much of it as that shape allows, and a fixed resolution is drawn at that size. Scale shows the image from fitted to the panel up to five times its size, and while the scene is stopped or paused the wheel zooms and the middle button pans, as in Unity. A playing scene reads the keyboard and mouse only while the panel has the focus, with the mouse in the image's pixels, and the keys are the game's rather than the toolbar's. Actions shows how each player reads its actions over the image
class GamePanel final : public Panel
{
public:
    explicit GamePanel(EditorSession& session);
    ~GamePanel() override;

    // Submits the scene to the panel's view, while the panel is shown
    void PrepareScene();
    // What the scene may read of the keyboard and mouse from the next frame, as the panel was drawn in this one, and none of either when it was not, once it is closed or behind another tab
    [[nodiscard]] Trinity::SceneInputFilter TakeSceneFilter();

    [[nodiscard]] bool ReadSetting(std::string_view key, std::string_view value);
    void WriteSettings(ImGuiTextBuffer& buffer) const;

    // Unity's Mute Audio, which silences the game while it plays
    [[nodiscard]] bool IsMutingAudio() const { return m_MuteAudio; }

protected:
    void OnImGuiRender() override;

private:
    enum class Aspect : std::uint8_t
    {
        Free,
        Ratio5x4,
        Ratio4x3,
        Ratio3x2,
        Ratio16x10,
        Ratio16x9,
        Fixed1280x720,
        Fixed1920x1080,
        Fixed2560x1440,
        Fixed3840x2160,

        Count
    };

    // Where the image is drawn, from its top left, and how many pixels of the panel each of its own is shown at
    struct ImageArea
    {
        glm::vec2 Min{ 0.0f };
        glm::vec2 Size{ 0.0f };
        float Scale = 1.0f;
    };

    void DrawToolbar();
    [[nodiscard]] ImageArea DrawImage(ImVec2 start, ImVec2 available, std::uint32_t index);
    void DrawStats(ImVec2 start, ImVec2 available) const;
    void DrawActions(ImVec2 start, ImVec2 available) const;
    void FilterSceneInput(const ImageArea& image, ImVec2 start, ImVec2 available);
    void FollowArea(std::uint32_t width, std::uint32_t height);
    void SetAspect(Aspect aspect);
    void SetScale(float scale);
    [[nodiscard]] float GetScale() const;
    [[nodiscard]] float GetFitScale() const;
    [[nodiscard]] glm::uvec2 GetWantedSize() const;

    EditorSession& m_Session;
    Trinity::ViewID m_View = Trinity::Renderer::c_MainView;
    Aspect m_Aspect = Aspect::Free;
    // The space under the toolbar, and when it last changed, so a size is taken once it settles
    std::uint32_t m_AreaWidth = 1;
    std::uint32_t m_AreaHeight = 1;
    double m_AreaTime = 0.0;
    // The scale the image is shown at, or none while it fits the space, and how far it is panned from the centre. A new aspect choice resets both
    std::optional<float> m_Scale;
    glm::vec2 m_Pan{ 0.0f };
    // Unity's Stats, the actions' overlay and Mute Audio, which imgui.ini keeps
    bool m_ShowStats = false;
    bool m_ShowActions = false;
    bool m_MuteAudio = false;
    // What the scene may read of the keyboard and mouse, as the panel was last drawn
    Trinity::SceneInputFilter m_SceneFilter{ .Keyboard = false, .Mouse = false, .MouseOver = false };
    // Whether the panel was drawn this frame, which only then submits the scene
    bool m_Shown = false;
};