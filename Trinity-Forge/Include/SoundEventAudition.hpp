#pragma once

#include <Trinity.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// One instance at a time, outside any scene, through the project's mixer, as PlayAtPoint plays it
class SoundEventAudition
{
public:
    SoundEventAudition() = default;
    ~SoundEventAudition();

    SoundEventAudition(const SoundEventAudition&) = delete;
    SoundEventAudition& operator=(const SoundEventAudition&) = delete;

    // In place of any instance playing, from the next Update, once the event and its clips have loaded
    void Play(Trinity::UUID event);
    // At once, or fading out over the seconds given
    void Stop(float fadeSeconds = 0.0f);
    // Once a frame, on the engine given. Its scene is made on first use. Nothing plays without one
    void Update(Trinity::AudioEngine* engine, Trinity::Timestep timestep);
    // Everything let go, as the project closes, the values given included
    void Clear();

    // From Play until the event ends or is stopped, its load and a fade out included
    [[nodiscard]] bool IsPlaying(Trinity::UUID event) const;
    // The event played last
    [[nodiscard]] Trinity::UUID GetEvent() const { return m_Event; }
    // Kept for later plays, and given at once to the instance playing. A global one is the engine's
    void SetParameter(std::string_view name, float value, bool global = false);
    // The value given, none before one is
    [[nodiscard]] std::optional<float> GetParameter(std::string_view name) const;
    // The engine's value of a global parameter, which a scene's script may have set. None before one is
    [[nodiscard]] std::optional<float> GetGlobalParameter(std::string_view name) const;
    // Where the instance playing has the parameter, its seek included. None while nothing plays
    [[nodiscard]] std::optional<float> GetHeardParameter(std::string_view name) const;
    // Every value given forgotten, as another event is opened
    void ClearParameters();

private:
    struct Value
    {
        std::string Name;
        float Number = 0.0f;
        bool Global = false;
    };

    Trinity::AudioEngine* m_Engine = nullptr;
    std::unique_ptr<Trinity::Scene> m_Scene;
    std::unique_ptr<Trinity::AudioScene> m_Audio;
    Trinity::UUID m_Event;
    Trinity::AudioHandle m_Handle;
    bool m_Requested = false;
    std::vector<Value> m_Values;
};