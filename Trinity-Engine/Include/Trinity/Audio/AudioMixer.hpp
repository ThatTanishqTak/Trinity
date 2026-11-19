#pragma once

#include "Trinity/Audio/AudioMixerData.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/UUID.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Trinity
{
    class AudioEngine;

    // Linear, after volume, mute and solo: a peak that falls 20 dB a second, and RMS over about 300 ms
    struct AudioMeter
    {
        static constexpr std::size_t c_MaxChannels = 8;

        std::uint32_t Channels = 0;
        std::array<float, c_MaxChannels> Peak{};
        std::array<float, c_MaxChannels> Rms{};
    };

    // Lives as long as the engine, so a mix carries on between scenes and between plays
    class TRINITY_API AudioMixer
    {
    public:
        // How long a change of mute or solo, or a value that would step, takes to reach the mix
        static constexpr std::uint32_t c_RampMilliseconds = 20;
        // How long an effect takes to cross to dry and back, as when bypassed or moved in its chain
        static constexpr std::uint32_t c_FadeMilliseconds = 10;
        // How far ahead a Limiter looks, which delays what passes through it by as much
        static constexpr std::uint32_t c_LimiterLookaheadMicroseconds = 2000;

        ~AudioMixer();

        AudioMixer(const AudioMixer&) = delete;
        AudioMixer& operator=(const AudioMixer&) = delete;

        // Changed in place: each group is found again by its ID, so sounds playing through it go on
        bool Apply(const AudioMixerData& data);
        [[nodiscard]] const AudioMixerData& GetData() const;

        // None for a group the mixer lacks
        [[nodiscard]] std::optional<AudioMeter> GetMeter(UUID group) const;
        // As heard now: held, set from code, or where the snapshots take it. None for an unknown value
        [[nodiscard]] std::optional<float> GetValue(AudioMixerValue value) const;

        // As Unity's TransitionTo. At once for a time of 0. False for an unknown snapshot
        bool TransitionToSnapshot(std::string_view name, float seconds);
        // As Unity's TransitionToSnapshots, the weights scaled to add up to 1. False for bad input
        bool TransitionToSnapshots(std::span<const std::string_view> names, std::span<const float> weights, float seconds);

        // As Unity's SetFloat: held through every transition until cleared. False for an unknown name
        bool SetFloat(std::string_view name, float value);
        // The value set, or where the snapshots have it now. None for an unknown name
        [[nodiscard]] std::optional<float> GetFloat(std::string_view name) const;
        // Back to where the snapshots take it. False for an unknown name
        bool ClearFloat(std::string_view name);

        // Back at the start snapshot at once, with every value set from code cleared
        void Reset();

        // For Forge: the mix plays this snapshot's values while transitions carry on beneath, unheard
        void Hold(UUID snapshot);

    private:
        friend class AudioEngine;
        friend struct AudioEngineInternal;

        struct State;

        explicit AudioMixer(AudioEngine& engine);

        // Once a frame and before each offline render, to step effects that have crossed to dry
        void Update();

        // Plays the node into the group by its ID, or Master, until it is sent elsewhere or let go
        void Route(void* node, UUID group);
        void Unroute(void* node);

        State* m_State = nullptr;
    };
}