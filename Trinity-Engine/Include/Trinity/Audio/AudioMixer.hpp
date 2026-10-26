#pragma once

#include "Trinity/Audio/AudioMixerData.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/UUID.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Trinity
{
    class AudioEngine;

    // A group's level as it leaves the group, after its volume, mute and solo, linear, for each of up to eight channels: its peak, which falls 20 dB a second once passed, and its RMS over about the last 300 ms
    struct AudioMeter
    {
        static constexpr std::size_t c_MaxChannels = 8;

        std::uint32_t Channels = 0;
        std::array<float, c_MaxChannels> Peak{};
        std::array<float, c_MaxChannels> Rms{};
    };

    // The project's mixer playing in an engine, which every sound sent to one of its groups goes through, as long as the engine lives, so a mix carries on between scenes and between plays. Each group is a miniaudio sound group that mixes the sounds played into it and the groups below it, at its pitch, where a pitch of 1 passes them through exactly and any other resamples them a sample late, then its effects in their order, Attenuation among them with its volume, then its mute and solo, all three ramped over 20 ms so no change steps, then its meter, and plays into its parent's, and Master into the engine's output. A sound sent to no group, or to one the mixer lacks, plays into Master. An effect's values glide to new ones over the same 20 ms, and its wet mix over 10 ms. Effects that move, arrive or leave cross to dry over 10 ms, step out of the chain or into it, and cross back, while those that keep their order play on, so a playing chain changes without a click, its echoes carrying on. A Send plays a copy of what passes it into the Receive or Duck Volume it names, in whatever group, in the same block, and stops before any change of the groups' parents would make what it sends come back to it. Main thread, but for the meters, which the audio thread writes and GetMeter reads without a lock
    class TRINITY_API AudioMixer
    {
    public:
        // How long a change of volume, mute, solo or an effect's value takes to reach the mix
        static constexpr std::uint32_t c_RampMilliseconds = 20;
        // How long an effect's wet mix takes to cross, as when it is bypassed, and as it steps out of its chain or into it
        static constexpr std::uint32_t c_FadeMilliseconds = 10;
        // How far ahead a Limiter looks, which delays what passes through it by as much
        static constexpr std::uint32_t c_LimiterLookaheadMicroseconds = 2000;

        ~AudioMixer();

        AudioMixer(const AudioMixer&) = delete;
        AudioMixer& operator=(const AudioMixer&) = delete;

        // The playing mix changed in place to the data's, at once: each group found again by its ID, and Master always Master, so the sounds playing through a group go on through it. What played into a group that is gone plays into Master, and what was sent to a group that comes back plays into it again. Data that breaks a mixer's rules is refused with an error, and the mix is left as it was
        bool Apply(const AudioMixerData& data);
        [[nodiscard]] const AudioMixerData& GetData() const;

        // None for a group the mixer lacks
        [[nodiscard]] std::optional<AudioMeter> GetMeter(UUID group) const;

    private:
        friend class AudioEngine;
        friend struct AudioEngineInternal;

        struct State;

        explicit AudioMixer(AudioEngine& engine);

        // Once a frame and before each offline render, so a chain whose effects have crossed to dry steps them out or in, and crosses them back
        void Update();

        // What a miniaudio node plays into: the group by its ID, or Master, until it is sent elsewhere or let go, which it is before it goes
        void Route(void* node, UUID group);
        void Unroute(void* node);

        State* m_State = nullptr;
    };
}