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

    // The volume a spatialized source has from its distance to the listener alone, from 0 to 1, as its voice is heard and ranked, the distance clamped between its minimum and maximum. Forge's Properties draws it as the source's rolloff curve
    [[nodiscard]] TRINITY_API float GetAudioRolloffGain(const AudioSourceComponent& source, float distance);

    // A sound played on its own, by PlayOneShot, PlayAtPoint or PlayAttached, which the scene's calls take to reach it. Each is new for the life of the process, so once its sound has ended, or the scene it played in has, it reaches nothing, and every call on it does nothing
    struct AudioHandle
    {
        std::uint64_t ID = 0;

        [[nodiscard]] explicit operator bool() const { return ID != 0; }
        [[nodiscard]] bool operator==(const AudioHandle&) const = default;
    };

    // Sounds taken from one scene to go on in the next, as SceneManager moves the entities kept across scenes, with no gap or restart
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

    // A running scene's AudioSources heard through its AudioListener, on an engine of the application's or an offline one. Each playing source is a voice, a sound event as much as a clip, whose clips its schedule starts on exact frames and its own mix plays together, its parameters' curves moving their volume, pitch and low-pass as they seek: the project's real voices are mixed, the most important first, then the loudest, and the rest are virtual, keeping their time unheard until they are mixed again from where they would be. Each real voice plays into the engine's mixer, in the group its source's Output names, or else its sound event's, or Master. A sound event plays as many instances at once as it allows, the oldest or the quietest as heard giving way over 20 ms to a new one when it says so, and no sooner after the last than its cooldown, counted across every voice of the running scene. Sounds follow their entities' interpolated transforms, read once a frame after the transform pass, and Doppler takes velocities from those moves. A source with Occlusion on is muffled while a ray from the listener to it is stopped by the scene's physics, on the project's occlusion layers, within the project's rays a frame. Everything it holds is under the Audio tag. Main thread
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

        // A source's occlusion as its last ray found it: how muffled it is, from 0, clear, to 1, eased towards whether that ray was stopped, and the ray, from the listener to the source, with where it was stopped, or the source where it was not
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

        // The worlds occlusion rays are cast into: the 3D one if the scene has it, and otherwise the 2D one, in its plane. Without either nothing is occluded
        void SetPhysics(const PhysicsScene3D* physics, const PhysicsScene2D* physics2D);
        // Once a frame, after the transform pass, with the frame's time: sources new since the last frame play if they Play On Awake, the voices of sources gone stop, the occluded ones are checked in turn, and the real voices are chosen again
        void Update(Timestep timestep);

        // From the start of its resource, in place of the source's voice playing, once its clip, or its sound event and every clip it names, has loaded. A source with neither plays nothing. A sound event plays with the source's volume, pitch, 3D settings and priority, into the source's Output or, when it names none, the event's, and loops as its containers say, whatever the source's Loop. Its choices are seeded by the event, the entity and how often the source has played, so a scene sounds the same on every run
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
        // Whether the source's voice plays unheard, as one beyond the real voices, without a listener, or paused
        [[nodiscard]] bool IsVirtual(Entity entity) const;
        // None for a source not playing, not occluded, or whose voice no ray has reached yet
        [[nodiscard]] std::optional<Occlusion> GetOcclusion(Entity entity) const;

        // A value of a parameter of the sound events the source plays, by its name, kept by the source for its later plays too, as Unity 6's AudioSource keeps its Audio Random Container's state, so a footstep's surface is set once and each step picks by it. A Switch reads it as it is triggered, and its label is the nearest, from 0. A continuous one is kept within its range, and every instance playing hears it from the same period on, seeking towards it at its speed. A global one is the engine's, which this leaves as it is. Allocates nothing once the source has played an event with the parameter. False for an entity without a source
        bool SetParameter(Entity entity, std::string_view name, float value);
        // By one of the parameter's labels, as the sound event the source names has them once it has loaded. False before then, or for a label it lacks
        bool SetParameterLabel(Entity entity, std::string_view name, std::string_view label);
        // The value the source was given, or else the default of the sound event it plays. None for neither
        [[nodiscard]] std::optional<float> GetParameter(Entity entity, std::string_view name) const;
        // Where the parameter is as the source's voice plays its sound event, its seek towards the value given included, as a global one's is towards the engine's. None for a source not playing an event that has it
        [[nodiscard]] std::optional<float> GetHeardParameter(Entity entity, std::string_view name) const;

        // As Unity's PlayOneShot: a clip or a sound event, or the source's own resource when none is given, played beside the source's voice rather than in its place, with the source's values and its volume scaled, following the source, and stopped, paused and kept across scenes with it. It shares the source's memory, so Avoid Repeat spans both. An invalid handle for an entity without a source or a source without a resource
        AudioHandle PlayOneShot(Entity entity, UUID resource = {}, float volumeScale = 1.0f);
        // As Unity's PlayClipAtPoint: a clip or a sound event at a place of its own, a clip spatialized as a source is by default, and a sound event with its own 3D settings, priority and Output, each play from a memory of its own, seeded by the event and how often the scene has played it so
        AudioHandle PlayAtPoint(UUID resource, const glm::vec3& position, float volume = 1.0f);
        // As PlayAtPoint, following the entity as a source would, until it is gone, then staying where it was, seeded by the entity too
        AudioHandle PlayAttached(UUID resource, Entity entity, float volume = 1.0f);

        // At once with no fade, and otherwise fading out over that many seconds, its sound still heard meanwhile, though no longer counted against its event's limit
        void Stop(AudioHandle handle, float fadeSeconds = 0.0f);
        // Until its sound ends, its fade out included
        [[nodiscard]] bool IsPlaying(AudioHandle handle) const;
        [[nodiscard]] double GetTime(AudioHandle handle) const;
        // A one-shot's are its source's, and any other's its own
        bool SetParameter(AudioHandle handle, std::string_view name, float value);
        bool SetParameterLabel(AudioHandle handle, std::string_view name, std::string_view label);
        [[nodiscard]] std::optional<float> GetParameter(AudioHandle handle, std::string_view name) const;
        [[nodiscard]] std::optional<float> GetHeardParameter(AudioHandle handle, std::string_view name) const;
        // Where a sound played at a point, or attached, plays from now on, no longer following an entity. Nothing for a one-shot, which follows its source
        void SetPosition(AudioHandle handle, const glm::vec3& position);
        // From now on following the entity, as PlayAttached does. Nothing for a one-shot
        void Follow(AudioHandle handle, Entity entity);

        // The engine's, as AudioEngine has them: a value every sound event playing with a global parameter by that name hears, from the same period. False, and none, without an engine
        bool SetGlobalParameter(std::string_view name, float value);
        [[nodiscard]] std::optional<float> GetGlobalParameter(std::string_view name) const;
        // The mixer of the engine the scene plays on, the project's, which its sounds play through. Null without an engine
        [[nodiscard]] AudioMixer* GetMixer() const;

        // Holds every voice where it is and keeps their time from moving, as a scene paused in Forge, whose Step plays nothing
        void SetPaused(bool paused);
        [[nodiscard]] bool IsPaused() const { return m_Paused; }

        // The voices of the entities, and whether each was a source already, so its Play On Awake does not play it again
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