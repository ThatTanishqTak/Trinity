#pragma once

#include "Trinity/Audio/AudioSettings.hpp"
#include "Trinity/Core/Screen.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"

#include <yaml-cpp/yaml.h>

#include <string>
#include <string_view>

// The physics, audio and window settings as a project's file, a game's Game.trgame and its player's Display.yaml hold them. owner begins each warning, as Project: Name or Game: Game.trgame
namespace Trinity::SettingsYaml
{
    // The key's text when it is a scalar, or the fallback
    [[nodiscard]] std::string ReadString(const YAML::Node& map, const char* key, std::string_view fallback);

    // The names given, by layer, which are all a layer has when there are any, so a name taken away stays away, and the pairs that do not collide, each lower layer first. A layer's name or pair that cannot be read is left out, and reported
    [[nodiscard]] CollisionLayers ReadCollisionLayers(const YAML::Node& physics, std::string_view owner);
    // Each world's gravity and the step rate, any missing kept at its default, and any that cannot be read reported and kept at its default
    void ReadWorldSettings(const YAML::Node& physics, std::string_view owner, PhysicsSettings& settings);
    // The mixer, the voice limits, the Doppler factor and occlusion, each that is missing kept at its default, and each that cannot be read reported and kept at its default. The occlusion layers are listed by number
    void ReadAudioSettings(const YAML::Node& audio, std::string_view owner, AudioSettings& settings);
    // The mode, the size and vsync, each that is missing kept as it is, and each that cannot be read reported and kept as it is
    void ReadWindowSettings(const YAML::Node& window, std::string_view owner, WindowSettings& settings);

    // A Physics map, of only the layer names that are not empty and the pairs that do not collide, so a new project's are short
    void WritePhysicsSettings(YAML::Emitter& emitter, const PhysicsSettings& settings);
    // An Audio map
    void WriteAudioSettings(YAML::Emitter& emitter, const AudioSettings& settings);
    // A Window map
    void WriteWindowSettings(YAML::Emitter& emitter, const WindowSettings& settings);
}