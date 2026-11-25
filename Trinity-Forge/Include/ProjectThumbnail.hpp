#pragma once

#include "EditorSession.hpp"

#include <Trinity.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>

// The project's picture in Hub, Cache/Thumbnail.png: the scene last saved, as its primary camera sees it
class ProjectThumbnail
{
public:
    // The longest a saved scene's assets are waited on before its picture is taken as it stands
    static constexpr std::chrono::seconds c_Wait{ 10 };

    explicit ProjectThumbnail(EditorSession& session) : m_Session(session) {}
    // Layers are destroyed before the Renderer, so the view can still go
    ~ProjectThumbnail();

    ProjectThumbnail(const ProjectThumbnail&) = delete;
    ProjectThumbnail& operator=(const ProjectThumbnail&) = delete;

    // As a scene is saved. Its picture is taken over the frames that follow
    void Request();
    // Once a frame, between frames. Draws the picture once the scene has loaded, and writes it once drawn
    void Update();
    // True for the one frame the scene is drawn into the picture, which Prepare then does
    [[nodiscard]] bool IsDrawing() const { return m_Draw; }
    // In OnPrepareRender, after the transform pass. sceneCamera is for a scene with no primary camera
    void Prepare(const Trinity::RenderView& sceneCamera);

private:
    enum class State : std::uint8_t
    {
        Idle,
        Waiting,
        Capturing
    };

    void Write(const Trinity::ViewCapture& capture) const;
    void Stop();

    EditorSession& m_Session;
    State m_State = State::Idle;
    // Where the picture goes, which also tells a project closed or changed since the save
    std::filesystem::path m_File;
    std::optional<Trinity::ViewID> m_View;
    std::chrono::steady_clock::time_point m_Deadline;
    bool m_Draw = false;
};