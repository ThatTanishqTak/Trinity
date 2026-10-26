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

    // The project's mixer playing in an engine, which every sound sent to one of its groups goes through, as long as the engine lives, so a mix carries on between scenes and between plays. Each group is a miniaudio sound group that mixes the sounds played into it and the groups below it, at its pitch, where a pitch of 1 passes them through exactly and any other resamples them a sample late, then its volume, with its mute and solo, all three ramped over 20 ms so no change steps, then its meter, and plays into its parent's, and Master into the engine's output. A sound sent to no group, or to one the mixer lacks, plays into Master. Main thread, but for the meters, which the audio thread writes and GetMeter reads without a lock
    class TRINITY_API AudioMixer
    {
    public:
        // How long a change of volume, mute or solo takes to reach the mix
        static constexpr std::uint32_t c_RampMilliseconds = 20;

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

        // What a miniaudio node plays into: the group by its ID, or Master, until it is sent elsewhere or let go, which it is before it goes
        void Route(void* node, UUID group);
        void Unroute(void* node);

        State* m_State = nullptr;
    };
}