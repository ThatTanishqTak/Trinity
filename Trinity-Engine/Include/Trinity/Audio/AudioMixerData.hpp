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
    // By Unity's names. What comes before Attenuation in the chain is before the fader
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

    // With its range and default. A value outside the range plays at its nearest end
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

    // Wet is in percent. Each keeps its ID through edits, so a playing effect keeps its state
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

    // As Unity's AudioMixerGroup. Volume is in decibels, the minimum being silence
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

    // How a value moves to a snapshot's during a transition, as Unity's transition overrides
    enum class AudioTransitionCurve : std::uint8_t
    {
        Linear,
        Smoothstep,
        Squared,
        SquareRoot,
        BrickwallStart,
        BrickwallEnd
    };

    // A group's Volume or Pitch, or an effect's Wet or parameter, by owner ID and slot
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

    // As Unity's snapshot. Mute, Solo, Bypass and the chains are the mixer's, the same in every one
    struct AudioMixerSnapshot
    {
        UUID ID;
        TaggedString<MemoryTag::Audio> Name;
        std::vector<AudioSnapshotValues, TaggedAllocator<AudioSnapshotValues, MemoryTag::Audio>> Values;
        std::vector<AudioSnapshotCurves, TaggedAllocator<AudioSnapshotCurves, MemoryTag::Audio>> Curves;

        [[nodiscard]] bool operator==(const AudioMixerSnapshot&) const = default;
    };

    // As Unity's exposed parameters. One set holds through every transition until cleared
    struct AudioExposedParameter
    {
        TaggedString<MemoryTag::Audio> Name;
        AudioMixerValue Value;

        [[nodiscard]] bool operator==(const AudioExposedParameter&) const = default;
    };

    // A .trmixer file: groups from Master, each after its parent, snapshots and exposed values
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

        // Master alone at 0 dB, as a project without a mixer is heard, and one snapshot named Snapshot
        [[nodiscard]] static AudioMixerData MakeDefault();

        // The first group, which a mixer that keeps to its rules has
        [[nodiscard]] const AudioMixerGroup& GetMaster() const;
        [[nodiscard]] const AudioMixerGroup* FindGroup(UUID id) const;
        [[nodiscard]] AudioMixerGroup* FindGroup(UUID id);
        // Never Attenuation, which has no ID
        [[nodiscard]] const AudioEffect* FindEffect(UUID id) const;
        [[nodiscard]] AudioEffect* FindEffect(UUID id);

        // The one StartSnapshot names, which a mixer that keeps to its rules has
        [[nodiscard]] const AudioMixerSnapshot& GetStartSnapshot() const;
        [[nodiscard]] const AudioMixerSnapshot* FindSnapshot(UUID id) const;
        [[nodiscard]] AudioMixerSnapshot* FindSnapshot(UUID id);
        [[nodiscard]] const AudioMixerSnapshot* FindSnapshot(std::string_view name) const;
        [[nodiscard]] const AudioExposedParameter* FindExposed(std::string_view name) const;

        // None for a value the mixer lacks
        [[nodiscard]] std::optional<AudioEffectParameter> GetValueInfo(AudioMixerValue value) const;
        // 2 for a group, parameters plus Wet for an effect, and 0 for an unknown ID
        [[nodiscard]] std::size_t GetSlotCount(UUID owner) const;
        // The start snapshot's is the group's or effect's own. None for a value the mixer lacks
        [[nodiscard]] std::optional<float> GetValue(const AudioMixerSnapshot& snapshot, AudioMixerValue value) const;
        [[nodiscard]] AudioTransitionCurve GetCurve(const AudioMixerSnapshot& snapshot, AudioMixerValue value) const;
        // False for a snapshot or a value the mixer lacks
        bool SetValue(UUID snapshot, AudioMixerValue value, float number);
        bool SetCurve(UUID snapshot, AudioMixerValue value, AudioTransitionCurve curve);
        // As Unity's Duplicate: a copy of the snapshot named, or of the start snapshot, with a new ID
        AudioMixerSnapshot& AddSnapshot(std::string_view name, UUID copyOf = UUID());
        // Any but the start snapshot. False for it, or for one the mixer lacks
        bool RemoveSnapshot(UUID snapshot);
        // As Unity's Set as Start Snapshot. False for a snapshot the mixer lacks
        bool SetStartSnapshot(UUID snapshot);

        // After every group below the parent already, with a new ID. Null for an unknown parent
        AudioMixerGroup* AddGroup(UUID parent, std::string_view name);
        // Copies get new IDs. A Send among them to a Receive among them sends to the copy
        AudioMixerGroup* DuplicateGroup(UUID group, std::string_view name);
        // With every group below it. False for Master or an unknown group
        bool RemoveGroup(UUID group);
        // False for Master, an unknown group or parent, or a parent that is the group or below it
        bool MoveGroup(UUID group, UUID parent, UUID before = UUID());
        // Placed as GetChain counts, or last. Null for Attenuation, or an unknown group
        AudioEffect* InsertEffect(UUID group, std::size_t place, AudioEffectType type);
        // False for an effect the mixer lacks
        bool RemoveEffect(UUID effect);
        // Places are as GetChain counts them. False for an unknown group or a place beyond its chain
        bool MoveEffect(UUID group, std::size_t from, std::size_t to);
        // Lets go of what snapshots, names and Sends hold for groups and effects that are gone
        void Prune();

        // The group's chain as it plays, its Attenuation last when its effects leave it out
        [[nodiscard]] static AudioEffectList GetChain(const AudioMixerGroup& group);

        [[nodiscard]] bool operator==(const AudioMixerData&) const = default;
    };

    // Unity's names for the curves, as a file and Forge's panel give them
    [[nodiscard]] TRINITY_API std::string_view GetAudioTransitionCurveKey(AudioTransitionCurve curve);
    [[nodiscard]] TRINITY_API std::optional<AudioTransitionCurve> FindAudioTransitionCurve(std::string_view key);
    // Both from 0 to 1. Brickwall Start is all the way after 0, and Brickwall End only at 1
    [[nodiscard]] TRINITY_API float EvaluateAudioTransitionCurve(AudioTransitionCurve curve, float progress);

    // Linear, 0 at the minimum volume and below, and the other way, the minimum for 0
    [[nodiscard]] TRINITY_API float AudioDecibelsToGain(float decibels);
    [[nodiscard]] TRINITY_API float AudioGainToDecibels(float gain);

    // Empty for a mixer that keeps to its rules, and otherwise the rule it breaks
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
        // A number of its own for each version loaded, so a reload is told from the one before
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

    // YAML: groups in order with their effects, then snapshots, with only the curves not Linear
    [[nodiscard]] TRINITY_API std::string WriteAudioMixer(const AudioMixerData& data);
    // A missing key keeps its default. A file without snapshots starts at one named Snapshot
    [[nodiscard]] TRINITY_API Expected<AudioMixerData, std::string> ParseAudioMixer(std::string_view text);
}