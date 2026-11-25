#pragma once

#include "EditorCamera3D.hpp"
#include "Modules/IdeLauncher.hpp"

#include <glm/glm.hpp>

#include <array>
#include <string>
#include <string_view>

// The same in every project, as Unity's EditorPrefs, kept in /saves/Preferences.txt as name=value
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
    // Minutes between backups of a scene with unsaved changes, and the most that can be asked for
    static constexpr int c_DefaultBackupMinutes = 5;
    static constexpr int c_MaximumBackupMinutes = 60;

    ScriptEditor Editor = ScriptEditor::System;
    bool WaitForDebugger = false;
    glm::vec3 PlayModeTint = c_DefaultPlayModeTint;
    // The 3D camera's flying speed in a scene not shown before, in units a second
    float CameraSpeed = EditorCamera3D::c_DefaultSpeed;
    glm::vec3 SnapSteps = c_DefaultSnapSteps;
    // By the name the system shows, or empty for its default. One that is missing is kept for later
    std::string AudioDevice;
    // Silence while none of Forge's windows has the focus. Off, sound goes on, as in Unity's editor
    bool MuteWhenUnfocused = false;
    // How often a scene with unsaved changes is backed up, for a crash to be recovered from. 0 backs it up only as Play starts
    int BackupMinutes = c_DefaultBackupMinutes;
    // Forge's own title bar, holding the menus, in place of the system's. Off, or where the platform keeps its own, the menus are a row under the system's
    bool CustomTitleBar = true;
};