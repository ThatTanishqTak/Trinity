#pragma once

#include "EditorSession.hpp"

#include <Trinity.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>

// --capture=<file.png>: the scene through its primary camera, once loaded. Then Forge exits 0, or 1
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
    // Once a frame, after the session's update. Takes the capture once the scene is ready
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