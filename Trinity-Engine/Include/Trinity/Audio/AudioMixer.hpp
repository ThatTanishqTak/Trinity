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

    // A group's level as it leaves the group, after its volume, mute and solo, linear, for each of up to eight channels: its peak, which falls 20 dB a second once passed, and its RMS over about the last 300 ms
    struct AudioMeter
    {
        static constexpr std::size_t c_MaxChannels = 8;

        std::uint32_t Channels = 0;
        std::array<float, c_MaxChannels> Peak{};
        std::array<float, c_MaxChannels> Rms{};
    };

    // The project's mixer playing in an engine, which every sound sent to one of its groups goes through, as long as the engine lives, so a mix carries on between scenes and between plays. Each group is a miniaudio sound group that mixes the sounds played into it and the groups below it, at its pitch, where a pitch of 1 passes them through exactly and any other resamples them a sample late, then its effects in their order, Attenuation among them with its volume, then its mute and solo, all three ramped over 20 ms so no change steps, then its meter, and plays into its parent's, and Master into the engine's output. A sound sent to no group, or to one the mixer lacks, plays into Master. Effects that move, arrive or leave cross to dry over 10 ms, step out of the chain or into it, and cross back, while those that keep their order play on, so a playing chain changes without a click, its echoes carrying on, and Bypass crosses over the same 10 ms. A Send plays a copy of what passes it into the Receive or Duck Volume it names, in whatever group, in the same block, and stops before any change of the groups' parents would make what it sends come back to it. Each group's volume and pitch and each effect's Wet and values are where the snapshots take them, which the audio thread follows on the engine's clock to the block, volume a 32-frame step at a time, so a transition reaches its values when it ends, each along its curve, and a value code sets holds until it is cleared. A change that would step a value, as an edit, a value set or cleared, or an instant transition, glides over 20 ms instead, a group's volume evenly in gain as its fader always has, but for an Echo's Delay and an SFX Reverb's Density, which make the effect anew and cross over to it as a transition ends. Main thread, but for the meters, which the audio thread writes and GetMeter reads without a lock
    class TRINITY_API AudioMixer
    {
    public:
        // How long a change of mute or solo, or a value that would step, takes to reach the mix
        static constexpr std::uint32_t c_RampMilliseconds = 20;
        // How long an effect takes to cross to dry and back, as when it is bypassed, and as it steps out of its chain or into it
        static constexpr std::uint32_t c_FadeMilliseconds = 10;
        // How far ahead a Limiter looks, which delays what passes through it by as much
        static constexpr std::uint32_t c_LimiterLookaheadMicroseconds = 2000;

        ~AudioMixer();

        AudioMixer(const AudioMixer&) = delete;
        AudioMixer& operator=(const AudioMixer&) = delete;

        // The playing mix changed in place to the data's, at once: each group found again by its ID, and Master always Master, so the sounds playing through a group go on through it. What played into a group that is gone plays into Master, and what was sent to a group that comes back plays into it again. Its values move to the new data's snapshots, a transition under way carrying on towards them, and one set from code that the data still exposes holds. Data that breaks a mixer's rules is refused with an error, and the mix is left as it was
        bool Apply(const AudioMixerData& data);
        [[nodiscard]] const AudioMixerData& GetData() const;

        // None for a group the mixer lacks
        [[nodiscard]] std::optional<AudioMeter> GetMeter(UUID group) const;

        // As Unity's AudioMixerSnapshot.TransitionTo: every value from where it is to the snapshot's over the time, each along the snapshot's curve for it, and at once for 0. False, and no change, for a name the mixer has no snapshot of
        bool TransitionToSnapshot(std::string_view name, float seconds);
        // As Unity's TransitionToSnapshots: to the snapshots' values weighted together, the weights scaled to add up to 1, each snapshot's share of the way along its own curve. False, and no change, for a name the mixer lacks, a weight below 0, no weight above it, or lists of two lengths
        bool TransitionToSnapshots(std::span<const std::string_view> names, std::span<const float> weights, float seconds);

        // As Unity's SetFloat: the exposed value at this, within its range, holding through every transition until it is cleared. False for a name the mixer does not expose
        bool SetFloat(std::string_view name, float value);
        // The value set, or else where the snapshots have it now. None for a name the mixer does not expose
        [[nodiscard]] std::optional<float> GetFloat(std::string_view name) const;
        // Back to where the snapshots take it. False for a name the mixer does not expose
        bool ClearFloat(std::string_view name);

        // Back at the start snapshot at once, every value set from code cleared, as Forge has it when Play starts and stops
        void Reset();

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