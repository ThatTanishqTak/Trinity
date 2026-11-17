#pragma once

#include "Trinity/Audio/AudioSettings.hpp"
#include "Trinity/Core/Screen.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"

#include <yaml-cpp/yaml.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

// owner begins each warning, as Project: Name or Game: Game.trgame
namespace Trinity::SettingsYaml
{
    [[nodiscard]] std::string ReadString(const YAML::Node& map, const char* key, std::string_view fallback);

    // The names given are all a layer has, so one taken away stays away. Unreadable ones are reported
    [[nodiscard]] CollisionLayers ReadCollisionLayers(const YAML::Node& physics, std::string_view owner);
    // A missing setting keeps its default. One that cannot be read is reported, and keeps it too
    void ReadWorldSettings(const YAML::Node& physics, std::string_view owner, PhysicsSettings& settings);
    // As ReadWorldSettings. The occlusion layers are listed by number
    void ReadAudioSettings(const YAML::Node& audio, std::string_view owner, AudioSettings& settings);
    // A missing or unreadable setting is kept as it is
    void ReadWindowSettings(const YAML::Node& window, std::string_view owner, WindowSettings& settings);
    // Appends each once. False when a name is no API, which is left out for the caller to report
    [[nodiscard]] bool ReadGraphicsAPIs(const YAML::Node& list, std::vector<GraphicsAPI>& apis);

    // Writes only the layer names that are not empty and the pairs that do not collide
    void WritePhysicsSettings(YAML::Emitter& emitter, const PhysicsSettings& settings);
    void WriteAudioSettings(YAML::Emitter& emitter, const AudioSettings& settings);
    void WriteWindowSettings(YAML::Emitter& emitter, const WindowSettings& settings);
    // On one line
    void WriteGraphicsAPIs(YAML::Emitter& emitter, std::span<const GraphicsAPI> apis);
}