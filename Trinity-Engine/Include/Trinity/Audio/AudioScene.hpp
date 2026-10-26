#pragma once

#include "Trinity/Audio/AudioSettings.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Core/UUID.hpp"

#include <cstdint>
#include <span>

namespace Trinity
{
    class AudioEngine;
    class Entity;
    class Scene;

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

    // A running scene's AudioSources heard through its AudioListener, on an engine of the application's or an offline one. Each playing source is a voice: the project's real voices are mixed, the most important first, then the loudest, and the rest are virtual, keeping their time unheard until they are mixed again from where they would be. Sounds follow their entities' interpolated transforms, read once a frame after the transform pass, and Doppler takes velocities from those moves. Everything it holds is under the Audio tag. Main thread
    class TRINITY_API AudioScene
    {
    public:
        struct Statistics
        {
            std::uint32_t Real = 0;
            std::uint32_t Virtual = 0;
            // Real voices reading their clips from files
            std::uint32_t Streams = 0;
        };

        // Without an engine every voice is virtual, so sounds still keep their time
        AudioScene(Scene& scene, AudioEngine* engine, const AudioSettings& settings = {});
        ~AudioScene();

        AudioScene(const AudioScene&) = delete;
        AudioScene& operator=(const AudioScene&) = delete;

        // Once a frame, after the transform pass, with the frame's time: sources new since the last frame play if they Play On Awake, the voices of sources gone stop, and the real voices are chosen again
        void Update(Timestep timestep);

        // From the start of its resource, in place of the source's voice playing, once its clip has loaded. A source with no clip plays nothing
        void Play(Entity entity);
        void Stop(Entity entity);
        // Holds the source's voice where it is, until UnPause
        void Pause(Entity entity);
        void UnPause(Entity entity);
        // From Play until the voice ends or stops, while it waits for its clip too. Not while it is paused
        [[nodiscard]] bool IsPlaying(Entity entity) const;
        // Where the voice is in its clip, in seconds. 0 without one
        [[nodiscard]] double GetTime(Entity entity) const;
        // Whether the source's voice plays unheard, as one beyond the real voices, without a listener, or paused
        [[nodiscard]] bool IsVirtual(Entity entity) const;

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