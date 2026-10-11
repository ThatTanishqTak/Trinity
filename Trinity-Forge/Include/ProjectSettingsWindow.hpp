#pragma once

#include <Trinity.hpp>

#include <array>
#include <string>

class EditorSession;

// Edit > Project Settings: the open project's settings, each saved with the project as it is changed, outside the scene's history. So far its physics: a name for each collision layer, and which pairs collide, as a triangle of boxes with a row and a column for each layer
class ProjectSettingsWindow
{
public:
    explicit ProjectSettingsWindow(EditorSession& session);

    void Draw();
    void Open();

private:
    void DrawPhysics(const Trinity::Project& project);

    EditorSession& m_Session;
    std::string m_Title;
    // A name being typed, kept apart from the project's until the field is left
    std::array<std::string, Trinity::c_MaxCollisionLayers> m_Names;
    bool m_Open = false;
    bool m_FocusRequested = false;
};