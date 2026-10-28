#pragma once

#include "EditorCamera3D.hpp"
#include "Modules/IdeLauncher.hpp"

#include <glm/glm.hpp>

#include <array>
#include <string>
#include <string_view>

// Forge's settings for the person using it, the same in every project, as Unity's EditorPrefs are, kept in /saves/Preferences.txt as name=value lines. The UI's scale, theme and multi-viewport are the ui.* console variables, which the file sets as Forge starts unless the command line sets them, for that run alone
struct EditorPreferences
{
    static constexpr std::string_view c_Path = "/saves/Preferences.txt";
    // The console variables kept with the rest, which the General section shows
    static constexpr std::array<std::string_view, 3> c_Variables{ "ui.scale", "ui.theme", "ui.viewports" };
    // The backgrounds' colour while a scene plays, Unity's Playmode tint
    static constexpr glm::vec3 c_DefaultPlayModeTint{ 0.2f, 0.35f, 0.65f };
    // Move in units, rotate in degrees, and scale
    static constexpr glm::vec3 c_DefaultSnapSteps{ 0.5f, 15.0f, 0.1f };
    static constexpr float c_MinimumSnap = 0.001f;
    static constexpr float c_MaximumSnap = 1000.0f;

    ScriptEditor Editor = ScriptEditor::System;
    bool WaitForDebugger = false;
    glm::vec3 PlayModeTint = c_DefaultPlayModeTint;
    // The 3D camera's flying speed in a scene the Scene panel has not shown before, in units a second. Each scene keeps its own as the wheel changes it while flying
    float CameraSpeed = EditorCamera3D::c_DefaultSpeed;
    glm::vec3 SnapSteps = c_DefaultSnapSteps;
    // The output Forge plays through, by the name the system shows, or empty for the system's default, which Forge follows as it changes. One that is not there as Forge starts leaves the default playing, and is kept for when it is back
    std::string AudioDevice;
    // Silence while none of Forge's windows has the focus. Off, sound goes on while an IDE is in front, as in Unity's editor
    bool MuteWhenUnfocused = false;
};