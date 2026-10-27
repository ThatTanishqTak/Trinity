#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // A unit of a group's chain, by Unity's names. Attenuation is where the group's volume applies, so what comes before it is before the fader and what comes after it after. A Send passes on what comes into it and sends a copy at its level to a Receive, which adds what is sent to it to what comes into it, or to a Duck Volume, which lowers what comes into it while what is sent to it is loud
    enum class AudioEffectType : std::uint8_t
    {
        Attenuation,
        Lowpass,
        Highpass,
        Bandpass,
        Notch,
        ParamEQ,
        LowShelf,
        HighShelf,
        Echo,
        Compressor,
        Limiter,
        SFXReverb,
        Send,
        Receive,
        DuckVolume
    };

    // One of an effect's values as the file names it and Step 8's panel shows it, with its range and default. A value outside the range is played at its nearest end
    struct AudioEffectParameter
    {
        std::string_view Name;
        std::string_view Key;
        std::string_view Unit;
        float Minimum = 0.0f;
        float Maximum = 0.0f;
        float Default = 0.0f;
    };

    struct AudioEffectInfo
    {
        AudioEffectType Type = AudioEffectType::Attenuation;
        std::string_view Name;
        std::string_view Key;
        std::span<const AudioEffectParameter> Parameters;
    };

    // An effect on a group: its values in the order of its type's parameters, the rest 0, its wet mix in percent, which crosses from what comes into it at 0 to what it makes at 100, and Bypass, which passes it by. Each keeps its ID through edits, so a playing effect keeps its state, its echoes among it. Attenuation has neither, nor an ID, since it is the group's volume. A Send names the Receive or Duck Volume it sends to by its ID, and sends nothing while it names none or one the mixer no longer has, as Unity's shows None
    struct AudioEffect
    {
        static constexpr std::size_t c_MaxParameters = 16;

        UUID ID;
        UUID Target;
        AudioEffectType Type = AudioEffectType::Attenuation;
        bool Bypass = false;
        float Wet = 100.0f;
        std::array<float, c_MaxParameters> Parameters{};

        [[nodiscard]] bool operator==(const AudioEffect&) const = default;
    };

    using AudioEffectList = std::vector<AudioEffect, TaggedAllocator<AudioEffect, MemoryTag::Audio>>;

    // Every type's name, key and parameters, in the enum's order
    [[nodiscard]] TRINITY_API std::span<const AudioEffectInfo> GetAudioEffectInfos();
    [[nodiscard]] TRINITY_API const AudioEffectInfo& GetAudioEffectInfo(AudioEffectType type);
    [[nodiscard]] TRINITY_API std::optional<AudioEffectType> FindAudioEffectType(std::string_view key);
    // At its type's defaults, with an ID of its own but for Attenuation
    [[nodiscard]] TRINITY_API AudioEffect MakeAudioEffect(AudioEffectType type);

    // One group of an Audio Mixer, as Unity's AudioMixerGroup: the sounds played into it and the groups below it mixed together, then played at its pitch, which changes their speed with it as a tape's does, and its volume in decibels, where the minimum is silence. Mute silences it and everything below it. While any group is soloed only the soloed groups are heard, with the groups below them and the groups above that carry them, and a group that only carries a soloed one passes on nothing played into it directly. Its effects run in order after the pitch, with Attenuation among them, which comes last when they leave it out, and Bypass passes every effect but Attenuation by. Mute and solo, then the meter, follow the last
    struct AudioMixerGroup
    {
        UUID ID;
        // None for Master, which every other group is below
        UUID Parent;
        TaggedString<MemoryTag::Audio> Name;
        float Volume = 0.0f;
        float Pitch = 1.0f;
        bool Mute = false;
        bool Solo = false;
        bool Bypass = false;
        AudioEffectList Effects;

        [[nodiscard]] bool operator==(const AudioMixerGroup&) const = default;
    };

    // How a value moves to a snapshot's as the mixer transitions to it, Unity's transition overrides: evenly, eased in and out, slowly then fast, fast then slowly, all at once at the start, or all at once at the end
    enum class AudioTransitionCurve : std::uint8_t
    {
        Linear,
        Smoothstep,
        Squared,
        SquareRoot,
        BrickwallStart,
        BrickwallEnd
    };

    // One of the values a snapshot holds, a name exposes and code sets: a group's Volume or Pitch, or an effect's Wet or one of its parameters, by the ID of the group or the effect and the value's slot
    struct AudioMixerValue
    {
        // A group's slots
        static constexpr std::uint8_t c_Volume = 0;
        static constexpr std::uint8_t c_Pitch = 1;
        // An effect's, its parameters from 1 in their order
        static constexpr std::uint8_t c_Wet = 0;
        static constexpr std::size_t c_MaxSlots = AudioEffect::c_MaxParameters + 1;

        UUID Owner;
        std::uint8_t Slot = 0;

        [[nodiscard]] bool operator==(const AudioMixerValue&) const = default;
    };

    // A snapshot's values for one group or effect, in its slots' order, the rest 0
    struct AudioSnapshotValues
    {
        UUID Owner;
        std::array<float, AudioMixerValue::c_MaxSlots> Values{};

        [[nodiscard]] bool operator==(const AudioSnapshotValues&) const = default;
    };

    // The curves a snapshot's values for one group or effect are reached along, in its slots' order
    struct AudioSnapshotCurves
    {
        UUID Owner;
        std::array<AudioTransitionCurve, AudioMixerValue::c_MaxSlots> Curves{};

        [[nodiscard]] bool operator==(const AudioSnapshotCurves&) const = default;
    };

    // Unity's Audio Mixer Snapshot: a value for each group's Volume and Pitch and each effect's Wet and parameters, which the mixer transitions to, each along its curve, Linear for one it has none for. Mute, Solo, Bypass, the effects and where Sends go are the mixer's, the same in every snapshot. The start snapshot's values are the groups' and effects' own, so it holds curves alone, and every other holds a copy of its own, taking the start snapshot's for a group or effect it holds none of, such as one added after it
    struct AudioMixerSnapshot
    {
        UUID ID;
        TaggedString<MemoryTag::Audio> Name;
        std::vector<AudioSnapshotValues, TaggedAllocator<AudioSnapshotValues, MemoryTag::Audio>> Values;
        std::vector<AudioSnapshotCurves, TaggedAllocator<AudioSnapshotCurves, MemoryTag::Audio>> Curves;

        [[nodiscard]] bool operator==(const AudioMixerSnapshot&) const = default;
    };

    // A value given a name, which code sets, reads and clears, as Unity's exposed parameters. One set holds through every transition until it is cleared
    struct AudioExposedParameter
    {
        TaggedString<MemoryTag::Audio> Name;
        AudioMixerValue Value;

        [[nodiscard]] bool operator==(const AudioExposedParameter&) const = default;
    };

    // What a .trmixer file holds, Unity's Audio Mixer: a tree of groups from Master, which comes first, each group after its parent, its snapshots, one of which it starts at, and the values it exposes by name
    struct TRINITY_API AudioMixerData
    {
        static constexpr std::uint32_t c_FormatVersion = 1;
        static constexpr std::string_view c_Extension = ".trmixer";
        // As Unity's faders go, -80 dB being off
        static constexpr float c_MinimumVolume = -80.0f;
        static constexpr float c_MaximumVolume = 20.0f;
        // From 1% to 1000%, as Unity's group pitch goes
        static constexpr float c_MinimumPitch = 0.01f;
        static constexpr float c_MaximumPitch = 10.0f;

        std::vector<AudioMixerGroup, TaggedAllocator<AudioMixerGroup, MemoryTag::Audio>> Groups;
        std::vector<AudioMixerSnapshot, TaggedAllocator<AudioMixerSnapshot, MemoryTag::Audio>> Snapshots;
        UUID StartSnapshot;
        std::vector<AudioExposedParameter, TaggedAllocator<AudioExposedParameter, MemoryTag::Audio>> Exposed;

        // Master alone at 0 dB, as a project without a mixer is heard, and one snapshot, named Snapshot as Unity's first is, to start at
        [[nodiscard]] static AudioMixerData MakeDefault();

        // The first group, which a mixer that keeps to its rules has
        [[nodiscard]] const AudioMixerGroup& GetMaster() const;
        [[nodiscard]] const AudioMixerGroup* FindGroup(UUID id) const;
        [[nodiscard]] AudioMixerGroup* FindGroup(UUID id);

        // The one StartSnapshot names, which a mixer that keeps to its rules has
        [[nodiscard]] const AudioMixerSnapshot& GetStartSnapshot() const;
        [[nodiscard]] const AudioMixerSnapshot* FindSnapshot(UUID id) const;
        [[nodiscard]] AudioMixerSnapshot* FindSnapshot(UUID id);
        [[nodiscard]] const AudioMixerSnapshot* FindSnapshot(std::string_view name) const;
        [[nodiscard]] const AudioExposedParameter* FindExposed(std::string_view name) const;

        // The value's name, unit, range and default: Volume and Pitch, an effect's Wet, or its parameter's own. None for a value the mixer lacks
        [[nodiscard]] std::optional<AudioEffectParameter> GetValueInfo(AudioMixerValue value) const;
        // How many values a group or an effect has, 2 for a group, its parameters and its Wet for an effect, and 0 for an ID the mixer has neither of
        [[nodiscard]] std::size_t GetSlotCount(UUID owner) const;
        // The value as a snapshot has it: its own, or the group's or effect's for the start snapshot and for a group or effect it holds none of. None for a value the mixer lacks
        [[nodiscard]] std::optional<float> GetValue(const AudioMixerSnapshot& snapshot, AudioMixerValue value) const;
        [[nodiscard]] AudioTransitionCurve GetCurve(const AudioMixerSnapshot& snapshot, AudioMixerValue value) const;
        // Into the snapshot, or the group or effect for the start snapshot. False for a snapshot or a value the mixer lacks
        bool SetValue(UUID snapshot, AudioMixerValue value, float number);
        bool SetCurve(UUID snapshot, AudioMixerValue value, AudioTransitionCurve curve);
        // A copy of another snapshot's values, or the start snapshot's when it names none, under the name, with an ID of its own, as Unity's Duplicate makes one
        AudioMixerSnapshot& AddSnapshot(std::string_view name, UUID copyOf = UUID());

        [[nodiscard]] bool operator==(const AudioMixerData&) const = default;
    };

    // Unity's names for the curves, as a file and Step 8's panel give them
    [[nodiscard]] TRINITY_API std::string_view GetAudioTransitionCurveKey(AudioTransitionCurve curve);
    [[nodiscard]] TRINITY_API std::optional<AudioTransitionCurve> FindAudioTransitionCurve(std::string_view key);
    // How far a value has moved along the curve at a share of the way through a transition, both from 0 to 1. Brickwall Start has moved all the way after 0, and Brickwall End only at 1
    [[nodiscard]] TRINITY_API float EvaluateAudioTransitionCurve(AudioTransitionCurve curve, float progress);

    // Linear, 0 at the minimum volume and below, and the other way, the minimum for 0
    [[nodiscard]] TRINITY_API float AudioDecibelsToGain(float decibels);
    [[nodiscard]] TRINITY_API float AudioGainToDecibels(float gain);

    // Empty for a mixer that keeps to its rules, and otherwise what it breaks: there is a Master and it comes first, every other group's parent is a group before it, no two groups share an ID, every volume and pitch is a number, no group has more than one Attenuation, every other effect has an ID no other effect has, every effect's type is known and its values are numbers, every Send names a Receive or a Duck Volume, or nothing the mixer has, no Send makes a loop, as one to its own group, to a group below it, or to a group that sends back to it does, since what it sends would come back to it, every snapshot has a name and an ID no other has, StartSnapshot names one, which holds no values, every snapshot's values and curves are for groups and effects the mixer has, once each, and are numbers and curves, and every exposed value has a name no other has and is a value the mixer has that no other name exposes
    [[nodiscard]] TRINITY_API std::string ValidateAudioMixer(const AudioMixerData& data);

    class TRINITY_API AudioMixerAsset final : public Asset
    {
    public:
        static constexpr std::string_view c_AssetType = "AudioMixer";

        explicit AudioMixerAsset(AudioMixerData data);

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_AssetType;
        }

        [[nodiscard]] const AudioMixerData& GetData() const { return m_Data; }
        // A number of its own for each version loaded, so a reload is told from the version before, wherever it lies
        [[nodiscard]] std::uint64_t GetSerial() const { return m_Serial; }

    private:
        AudioMixerData m_Data;
        std::uint64_t m_Serial;
    };

    // Reads .trmixer files, which the asset registry imports as AudioMixer
    class TRINITY_API AudioMixerLoader final : public AssetLoader
    {
    public:
        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return AudioMixerAsset::c_AssetType;
        }

        [[nodiscard]] Expected<Asset*, std::string> Load(std::span<const std::byte> data) const override;
    };

    // YAML, the groups in their order, Master without a Parent, each with its effects in their order, unless it has none, then the snapshots in their order, each with its values for every group and effect but the start snapshot, and only the curves that are not Linear, then the start snapshot and the exposed values
    [[nodiscard]] TRINITY_API std::string WriteAudioMixer(const AudioMixerData& data);
    // A key that is missing keeps its default, and a snapshot's value the start snapshot's. A file without snapshots, as Steps 5 and 6 wrote, starts at one of its own named Snapshot. A malformed key, a mixer that breaks its rules, or a newer format, refuses the file
    [[nodiscard]] TRINITY_API Expected<AudioMixerData, std::string> ParseAudioMixer(std::string_view text);
}