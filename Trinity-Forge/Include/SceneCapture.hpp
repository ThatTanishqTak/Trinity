#pragma once

#include "EditorSession.hpp"

#include <Trinity.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>

// --capture=<file.png>: the open scene through its primary camera, as the Game panel shows it, written as a PNG of c_Width by c_Height once its imports have finished and everything it draws has loaded, then Forge closes, with 0 once it is written and 1 when it could not be, as a template's thumbnail is made
class SceneCapture
{
public:
    static constexpr std::uint32_t c_Width = 512;
    static constexpr std::uint32_t c_Height = 288;
    // Frames drawn with everything loaded before the capture, so what was uploaded last is drawn too
    static constexpr std::uint32_t c_SettleFrames = 30;
    static constexpr std::chrono::seconds c_Timeout{ 120 };

    explicit SceneCapture(EditorSession& session) : m_Session(session) {}
    // Layers are destroyed before the Renderer, so the view can still go
    ~SceneCapture();

    SceneCapture(const SceneCapture&) = delete;
    SceneCapture& operator=(const SceneCapture&) = delete;

    void Start(std::filesystem::path file);
    // Once a frame, after the session's update: whether the scene is ready, and the capture once it is back
    void Update();
    // In OnPrepareRender, after the transform pass
    void Prepare();

private:
    // Every asset the scene draws has loaded, or failed to
    [[nodiscard]] bool IsSceneLoaded() const;
    void Finish(bool written);

    EditorSession& m_Session;
    std::optional<std::filesystem::path> m_File;
    std::optional<Trinity::ViewID> m_View;
    std::chrono::steady_clock::time_point m_Deadline;
    std::uint32_t m_Settled = 0;
    bool m_Requested = false;
};