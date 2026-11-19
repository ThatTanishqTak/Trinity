#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Audio/AudioSettings.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // As FMOD's instruments: a Clip, a Random of its children, a Sequence, a Layer, or a Switch
    enum class SoundContainerType : std::uint8_t
    {
        Clip,
        Random,
        Sequence,
        Layer,
        Switch
    };

    // Pulse spaces triggers start to start, and Offset end to start, as Unity's Trigger
    enum class SoundTriggerTiming : std::uint8_t
    {
        Pulse,
        Offset
    };

    // As FMOD's stealing: stop the oldest, stop the quietest, or do not play the new one
    enum class SoundStealMode : std::uint8_t
    {
        Oldest,
        Quietest,
        None
    };

    // Continuous seeks towards each value given. Discrete jumps to a label, which a Switch picks by
    enum class SoundParameterKind : std::uint8_t
    {
        Discrete,
        Continuous
    };

    // Volume in decibels added, pitch as a speed multiplied, or a low-pass cutoff in hertz
    enum class SoundCurveTarget : std::uint8_t
    {
        Volume,
        Pitch,
        Cutoff
    };

    // As FMOD's automation: straight lines between points, level beyond the first and the last
    struct TRINITY_API SoundCurve
    {
        UUID Parameter;
        SoundCurveTarget Target = SoundCurveTarget::Volume;
        std::vector<glm::vec2, TaggedAllocator<glm::vec2, MemoryTag::Audio>> Points;

        // At the parameter's value. 0 for a curve with no points
        [[nodiscard]] float Evaluate(float value) const;

        [[nodiscard]] bool operator==(const SoundCurve&) const = default;
    };

    // Volume is in decibels and pitch a speed, each varied by up to its spread on every trigger
    struct SoundContainer
    {
        UUID ID;
        // None for the root, which every other container is below
        UUID Parent;
        TaggedString<MemoryTag::Audio> Name;
        SoundContainerType Type = SoundContainerType::Clip;
        float Volume = 0.0f;
        float VolumeRandom = 0.0f;
        float Pitch = 1.0f;
        // As a share of its pitch, 0.05 being 5% either way
        float PitchRandom = 0.0f;
        float Delay = 0.0f;
        float DelayRandom = 0.0f;
        float Weight = 1.0f;
        TaggedString<MemoryTag::Audio> Case;

        UUID Clip;
        bool Loop = false;
        std::uint32_t AvoidRepeat = 0;
        bool AutoTrigger = false;
        SoundTriggerTiming Timing = SoundTriggerTiming::Pulse;
        float IntervalMinimum = 1.0f;
        float IntervalMaximum = 1.0f;
        // Triggers in all, 0 for as long as the event plays
        std::uint32_t TriggerCount = 0;
        UUID Parameter;
        // At most one for each parameter and value
        std::vector<SoundCurve, TaggedAllocator<SoundCurve, MemoryTag::Audio>> Curves;

        [[nodiscard]] bool operator==(const SoundContainer&) const = default;
    };

    // Starts at its default. A global one takes the value the game gives every instance by name
    struct TRINITY_API SoundParameter
    {
        UUID ID;
        TaggedString<MemoryTag::Audio> Name;
        SoundParameterKind Kind = SoundParameterKind::Discrete;
        bool Global = false;
        // A discrete one's alone
        std::vector<TaggedString<MemoryTag::Audio>, TaggedAllocator<TaggedString<MemoryTag::Audio>, MemoryTag::Audio>> Labels;
        // A continuous one's alone
        float Minimum = 0.0f;
        float Maximum = 1.0f;
        float Default = 0.0f;
        float SeekSpeed = 0.0f;

        // Its lowest and highest value, a discrete one's its first label and its last
        [[nodiscard]] float GetMinimum() const;
        [[nodiscard]] float GetMaximum() const;
        // Within its range, a discrete one at its nearest label, and its default for no number
        [[nodiscard]] float Clamp(float value) const;

        [[nodiscard]] bool operator==(const SoundParameter&) const = default;
    };

    // A .trsound file: containers from the root, each after its parent, and the event's settings
    struct TRINITY_API SoundEventData
    {
        static constexpr std::uint32_t c_FormatVersion = 1;
        static constexpr std::string_view c_Extension = ".trsound";
        static constexpr float c_MinimumVolume = -80.0f;
        static constexpr float c_MaximumVolume = 20.0f;
        static constexpr float c_MaximumVolumeRandom = 40.0f;
        // As a source's pitch goes
        static constexpr float c_MinimumPitch = 0.01f;
        static constexpr float c_MaximumPitch = 3.0f;
        static constexpr float c_MaximumPitchRandom = 1.0f;
        // A low-pass curve's cutoff goes from here to where it is all but clear
        static constexpr float c_MinimumCutoff = 10.0f;
        static constexpr float c_MaximumCutoff = 22000.0f;
        static constexpr std::uint32_t c_LowestPriority = 256;

        std::vector<SoundContainer, TaggedAllocator<SoundContainer, MemoryTag::Audio>> Containers;
        std::vector<SoundParameter, TaggedAllocator<SoundParameter, MemoryTag::Audio>> Parameters;
        // A mixer group's ID. None, or a group the mixer lacks, plays into Master
        UUID Output;
        // From 0, the most important, to 256, the least, as a source's
        std::uint32_t Priority = 128;
        // Instances playing at once, 0 for any number
        std::uint32_t MaxInstances = 0;
        SoundStealMode Steal = SoundStealMode::Oldest;
        // Seconds after an instance starts before another may, which a play sooner does not
        float Cooldown = 0.0f;
        bool Spatialize = true;
        float DopplerLevel = 1.0f;
        float Spread = 0.0f;
        float MinDistance = 1.0f;
        float MaxDistance = 500.0f;
        AudioRolloff Rolloff = AudioRolloff::Logarithmic;
        bool Occlusion = false;
        float OcclusionVolume = 0.5f;
        float OcclusionCutoff = 1500.0f;

        // A Random named Root with nothing in it, as Unity's new Audio Random Container is empty
        [[nodiscard]] static SoundEventData MakeDefault();

        // The first container, which an event that keeps to its rules has
        [[nodiscard]] const SoundContainer& GetRoot() const;
        [[nodiscard]] const SoundContainer* FindContainer(UUID id) const;
        [[nodiscard]] SoundContainer* FindContainer(UUID id);
        [[nodiscard]] const SoundParameter* FindParameter(UUID id) const;
        [[nodiscard]] SoundParameter* FindParameter(UUID id);
        [[nodiscard]] const SoundParameter* FindParameter(std::string_view name) const;
        // The clip of each Clip container that names one, once each, in the containers' order
        [[nodiscard]] std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> GetClips() const;
        [[nodiscard]] const SoundCurve* FindCurve(UUID container, UUID parameter, SoundCurveTarget target) const;
        [[nodiscard]] SoundCurve* FindCurve(UUID container, UUID parameter, SoundCurveTarget target);

        // After everything below the parent already, with a new ID. Null for an unknown parent or a Clip
        SoundContainer* AddContainer(UUID parent, SoundContainerType type, std::string_view name, UUID clip = UUID());
        // Copies get new IDs. Null for the root or a container the event lacks
        SoundContainer* DuplicateContainer(UUID container, std::string_view name);
        // The container and everything below it. False for the root or a container the event lacks
        bool RemoveContainer(UUID container);
        // Its case is kept only below a Switch by the same parameter, and its weight below a Random
        bool MoveContainer(UUID container, UUID parent, UUID before = UUID());
        // What the new type never saves goes back to its default. False for an unknown container
        bool SetContainerType(UUID container, SoundContainerType type);
        // Its children's cases are let go when it changes. False unless a Switch and a discrete parameter
        bool SetSwitchParameter(UUID container, UUID parameter);
        // Continuous from 0 to 1, or discrete with one label. Null for an empty or taken name
        SoundParameter* AddParameter(std::string_view name, SoundParameterKind kind);
        // Its curves go, and Switches by it pick by none. False for one the event lacks
        bool RemoveParameter(UUID parameter);
        // False for one the event lacks, or a name that is empty or another parameter's
        bool RenameParameter(UUID parameter, std::string_view name);
        // Its default is kept within its range. False for one the event lacks
        bool SetParameterKind(UUID parameter, SoundParameterKind kind);
        // Adds, renames or removes a label, its cases following. False for bad input or its only label
        bool AddLabel(UUID parameter, std::string_view name);
        bool RenameLabel(UUID parameter, std::size_t index, std::string_view name);
        bool RemoveLabel(UUID parameter, std::size_t index);
        // Level at the value that changes nothing, or the curve it has already. Null for unknown input
        SoundCurve* AddCurve(UUID container, UUID parameter, SoundCurveTarget target);
        // False for a curve the container lacks
        bool RemoveCurve(UUID container, UUID parameter, SoundCurveTarget target);


        [[nodiscard]] bool operator==(const SoundEventData&) const = default;
    };

    // The names a file and Forge give each enum's values, in the enum's order
    [[nodiscard]] TRINITY_API std::string_view GetSoundContainerTypeKey(SoundContainerType type);
    [[nodiscard]] TRINITY_API std::optional<SoundContainerType> FindSoundContainerType(std::string_view key);
    [[nodiscard]] TRINITY_API std::string_view GetSoundTriggerTimingKey(SoundTriggerTiming timing);
    [[nodiscard]] TRINITY_API std::optional<SoundTriggerTiming> FindSoundTriggerTiming(std::string_view key);
    [[nodiscard]] TRINITY_API std::string_view GetSoundStealModeKey(SoundStealMode mode);
    [[nodiscard]] TRINITY_API std::optional<SoundStealMode> FindSoundStealMode(std::string_view key);
    [[nodiscard]] TRINITY_API std::string_view GetSoundParameterKindKey(SoundParameterKind kind);
    [[nodiscard]] TRINITY_API std::optional<SoundParameterKind> FindSoundParameterKind(std::string_view key);
    [[nodiscard]] TRINITY_API std::string_view GetSoundCurveTargetKey(SoundCurveTarget target);
    [[nodiscard]] TRINITY_API std::optional<SoundCurveTarget> FindSoundCurveTarget(std::string_view key);

    // Empty for an event that keeps to its rules, and otherwise the rule it breaks
    [[nodiscard]] TRINITY_API std::string ValidateSoundEvent(const SoundEventData& data);

    // It does not hold the clips it names, so whatever plays it holds them
    class TRINITY_API SoundEventAsset final : public Asset
    {
    public:
        static constexpr std::string_view c_AssetType = "SoundEvent";

        explicit SoundEventAsset(SoundEventData data);

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_AssetType;
        }

        [[nodiscard]] const SoundEventData& GetData() const { return m_Data; }
        // As GetData().GetClips(), found once as it loads
        [[nodiscard]] std::span<const UUID> GetClips() const { return m_Clips; }
        // A number of its own for each version loaded, so a reload is told from the one before
        [[nodiscard]] std::uint64_t GetSerial() const { return m_Serial; }

    private:
        SoundEventData m_Data;
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> m_Clips;
        std::uint64_t m_Serial;
    };

    // Reads .trsound files, which the asset registry imports as SoundEvent
    class TRINITY_API SoundEventLoader final : public AssetLoader
    {
    public:
        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return SoundEventAsset::c_AssetType;
        }

        [[nodiscard]] Expected<Asset*, std::string> Load(std::span<const std::byte> data) const override;
    };

    // YAML: the event's values, its parameters, then its containers in order, the root first
    [[nodiscard]] TRINITY_API std::string WriteSoundEvent(const SoundEventData& data);
    // A missing key keeps its default. A malformed key, a broken rule or a newer format is refused
    [[nodiscard]] TRINITY_API Expected<SoundEventData, std::string> ParseSoundEvent(std::string_view text);
}