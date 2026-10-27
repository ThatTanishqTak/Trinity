#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <array>
#include <string>

// The open project's settings, each edit a command in the history, so it undoes in one step, and saved with the project as it is made. Its audio, as Unity's Audio settings: the mixer, the voices mixed and playing at once, the Doppler factor and occlusion's layers, rays and time. Then its physics: each world's gravity, the step rate, a name for each collision layer, and which pairs collide, as a triangle of boxes with a row and a column for each layer. A scene playing keeps the settings it started with, so nothing here changes while one plays but the mixer, which the engine plays for everything at once
class ProjectSettingsPanel final : public Panel
{
public:
    explicit ProjectSettingsPanel(EditorSession& session);

protected:
    void OnImGuiRender() override;

private:
    void DrawAudio(Trinity::AudioSettings settings, const Trinity::CollisionLayers& layers, bool playing);
    void DrawMixer(Trinity::AudioSettings settings);
    void DrawWorlds(Trinity::PhysicsSettings settings);
    void DrawLayerNames(Trinity::PhysicsSettings settings);
    void DrawCollisionMatrix(Trinity::PhysicsSettings settings);

    EditorSession& m_Session;
    // A name being typed, kept apart from the project's until the field is left
    std::array<std::string, Trinity::c_MaxCollisionLayers> m_Names;
};