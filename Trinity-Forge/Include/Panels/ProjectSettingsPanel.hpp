#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <array>
#include <string>

// The project's audio and physics settings. Each edit is a command, saved with the project
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