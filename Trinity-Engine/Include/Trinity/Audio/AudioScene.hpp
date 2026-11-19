#pragma once

#include "Trinity/Audio/AudioSettings.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Core/UUID.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Trinity
{
    class AudioEngine;
    class AudioMixer;
    class Entity;
    class PhysicsScene2D;
    class PhysicsScene3D;
    class Scene;
    struct AudioSourceComponent;

    // From 0 to 1, by distance alone, clamped between its minimum and maximum
    [[nodiscard]] TRINITY_API float GetAudioRolloffGain(const AudioSourceComponent& source, float distance);

    // New for the life of the process: once its sound has ended, calls on it do nothing
    struct AudioHandle
    {
        std::uint64_t ID = 0;

        [[nodiscard]] explicit operator bool() const { return ID != 0; }
        [[nodiscard]] bool operator==(const AudioHandle&) const = default;
    };

    // Sounds taken from one scene to go on in the next, with no gap or restart
    class TRINITY_API AudioCarry
    {
    public:
        AudioCarry() = default;
        ~AudioCarry();

        AudioCarry(AudioCarry&& other) noexcept;
        AudioCarry& operator=(AudioCarry&& other) noexcept;

        AudioCarry(const AudioCarry&) = delete;
        AudioCarry& operator=(const AudioCarry&) = delete;

    private:
        friend class AudioScene;

        struct State;

        State* m_State = nullptr;
    };

    // Each playing source is a voice. Only the project's real voices are mixed, the rest keep time
    class TRINITY_API AudioScene
    {
    public:
        struct Statistics
        {
            std::uint32_t Real = 0;
            std::uint32_t Virtual = 0;
            // Voices playing a sound event, real or virtual
            std::uint32_t Events = 0;
            // Clips real voices read from files, a sound event's each
            std::uint32_t Streams = 0;
            // Occlusion rays cast in the last update
            std::uint32_t OcclusionRays = 0;
        };

        // From 0, clear, to 1, eased towards whether its last ray was stopped
        struct Occlusion
        {
            float Amount = 0.0f;
            bool Blocked = false;
            glm::vec3 From{ 0.0f };
            glm::vec3 To{ 0.0f };
            glm::vec3 Hit{ 0.0f };
        };

        // Without an engine every voice is virtual, so sounds still keep their time
        AudioScene(Scene& scene, AudioEngine* engine, const AudioSettings& settings = {});
        ~AudioScene();

        AudioScene(const AudioScene&) = delete;
        AudioScene& operator=(const AudioScene&) = delete;

        // The 3D world if the scene has it, else the 2D one. Without either nothing is occluded
        void SetPhysics(const PhysicsScene3D* physics, const PhysicsScene2D* physics2D);
        // Once a frame, after the transform pass. The real voices are chosen again
        void Update(Timestep timestep);

        // From the start, once its clip or event has loaded. Replaces the source's voice playing
        void Play(Entity entity);
        // The source's voice and its one-shots, at once, as Unity's Stop
        void Stop(Entity entity);
        // Holds the source's voice and its one-shots where they are, until UnPause
        void Pause(Entity entity);
        void UnPause(Entity entity);
        // From Play until the voice ends or stops, while it waits for its clip too. Not while it is paused
        [[nodiscard]] bool IsPlaying(Entity entity) const;
        // Where the voice is in its clip, or how long its sound event has played, in seconds. 0 without one
        [[nodiscard]] double GetTime(Entity entity) const;
        // Whether the voice plays unheard: beyond the real voices, without a listener, or paused
        [[nodiscard]] bool IsVirtual(Entity entity) const;
        // None for a source not playing, not occluded, or whose voice no ray has reached yet
        [[nodiscard]] std::optional<Occlusion> GetOcclusion(Entity entity) const;

        // Kept by the source for its later plays too. A Switch reads it as it is triggered
        bool SetParameter(Entity entity, std::string_view name, float value);
        // False before the event has loaded, or for a label it lacks
        bool SetParameterLabel(Entity entity, std::string_view name, std::string_view label);
        // The value the source was given, or else the default of the sound event it plays. None for neither
        [[nodiscard]] std::optional<float> GetParameter(Entity entity, std::string_view name) const;
        // As the voice plays it, its seek towards the value included. None when not playing
        [[nodiscard]] std::optional<float> GetHeardParameter(Entity entity, std::string_view name) const;

        // As Unity's PlayOneShot: beside the source's voice, following and stopping with the source
        AudioHandle PlayOneShot(Entity entity, UUID resource = {}, float volumeScale = 1.0f);
        // As Unity's PlayClipAtPoint. Each play is seeded by the event and its play count
        AudioHandle PlayAtPoint(UUID resource, const glm::vec3& position, float volume = 1.0f);
        // As PlayAtPoint, following the entity until it is gone, then staying where it was
        AudioHandle PlayAttached(UUID resource, Entity entity, float volume = 1.0f);
        // Never spatialized. It plays on through a scene change, so a menu click is not cut off
        AudioHandle PlayUI(UUID resource, UUID output = {}, float volume = 1.0f);

        // At once for 0, else fading out over that many seconds, no longer counted against its limit
        void Stop(AudioHandle handle, float fadeSeconds = 0.0f);
        // Until its sound ends, its fade out included
        [[nodiscard]] bool IsPlaying(AudioHandle handle) const;
        [[nodiscard]] double GetTime(AudioHandle handle) const;
        // A one-shot's are its source's, and any other's its own
        bool SetParameter(AudioHandle handle, std::string_view name, float value);
        bool SetParameterLabel(AudioHandle handle, std::string_view name, std::string_view label);
        [[nodiscard]] std::optional<float> GetParameter(AudioHandle handle, std::string_view name) const;
        [[nodiscard]] std::optional<float> GetHeardParameter(AudioHandle handle, std::string_view name) const;
        // Stops following an entity. Does nothing for a one-shot, which follows its source
        void SetPosition(AudioHandle handle, const glm::vec3& position);
        // From now on following the entity, as PlayAttached does. Nothing for a one-shot
        void Follow(AudioHandle handle, Entity entity);

        // The engine's global parameters. False, and none, without an engine
        bool SetGlobalParameter(std::string_view name, float value);
        [[nodiscard]] std::optional<float> GetGlobalParameter(std::string_view name) const;
        // The project's mixer, on the engine the scene plays on. Null without an engine
        [[nodiscard]] AudioMixer* GetMixer() const;

        // Holds every voice and stops their time, as a scene paused in Forge
        void SetPaused(bool paused);
        [[nodiscard]] bool IsPaused() const { return m_Paused; }

        // Whether each was a source already, so its Play On Awake does not play it again
        [[nodiscard]] AudioCarry Extract(std::span<const UUID> entities);
        void Adopt(AudioCarry&& carry);

        [[nodiscard]] Statistics GetStatistics() const;
        [[nodiscard]] const AudioSettings& GetSettings() const;

    private:
        struct State;

        State* m_State = nullptr;
        bool m_Paused = false;
    };
}