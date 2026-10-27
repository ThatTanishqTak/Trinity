#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Audio/AudioSettings.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // What a container of a sound event plays each time it is triggered, as a light FMOD's instruments are: a Clip its clip, a Random one of its children by their weights, a Sequence each of its children in turn, each starting on the sample the one before it ends, a Layer every child at once, from the same sample, and a Switch the children whose case is the label its parameter has as it is triggered, or else those with no case
    enum class SoundContainerType : std::uint8_t
    {
        Clip,
        Random,
        Sequence,
        Layer,
        Switch
    };

    // How a Random that triggers itself spaces its triggers, as Unity's Audio Random Container's Trigger does: Pulse from the start of one to the start of the next, and Offset from the end of one to the start of the next
    enum class SoundTriggerTiming : std::uint8_t
    {
        Pulse,
        Offset
    };

    // What a sound event does as it plays while its limit of instances already play, as FMOD's stealing: stop the one that began first, stop the quietest, unless the new one would be quieter still, and then not play it, or not play it
    enum class SoundStealMode : std::uint8_t
    {
        Oldest,
        Quietest,
        None
    };

    // One container of a sound event's tree. Each has a volume in decibels and a pitch, a speed of playing as a source's is, each varied at random by up to its spread either way each time it is triggered, and a delay in seconds before it starts, lengthened at random by up to its own spread. What else it holds is its type's: a Clip its clip, which loops while Loop is on, a Random how many of its last picks it never picks again, and whether it triggers itself, on an interval between two lengths, a number of times or for as long as the event plays, a Sequence whether it starts again from its first child once its last ends, and a Switch the parameter it picks by. A child of a Random has a weight, its share of the picks, and a child of a Switch a case, one of the parameter's labels, or none for every label no other child has. A value outside its range is played at its nearest end
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

        [[nodiscard]] bool operator==(const SoundContainer&) const = default;
    };

    // A value each playing instance of a sound event is given by name, starting at its default, which its Switches pick by: one of its labels, by its index from 0
    struct SoundParameter
    {
        UUID ID;
        TaggedString<MemoryTag::Audio> Name;
        std::vector<TaggedString<MemoryTag::Audio>, TaggedAllocator<TaggedString<MemoryTag::Audio>, MemoryTag::Audio>> Labels;
        float Default = 0.0f;

        [[nodiscard]] bool operator==(const SoundParameter&) const = default;
    };

    // What a .trsound file holds, a Sound Event: a tree of containers from its root, which comes first, each container after its parent and in its order among its parent's children, the parameters it is played with, how many instances of it play at once, how they give way and how soon one plays after another, the mixer group, priority and 3D settings it plays with on its own, as PlayAtPoint and Forge's audition play it, where an AudioSource playing it uses its own, and its Output group only when it names none
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
        static constexpr std::uint32_t c_LowestPriority = 256;

        std::vector<SoundContainer, TaggedAllocator<SoundContainer, MemoryTag::Audio>> Containers;
        std::vector<SoundParameter, TaggedAllocator<SoundParameter, MemoryTag::Audio>> Parameters;
        // One of the project's mixer's groups, by its ID. None, or a group the mixer lacks, plays into Master
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
        [[nodiscard]] const SoundParameter* FindParameter(std::string_view name) const;
        // The clip of each Clip container that names one, once each, in the containers' order
        [[nodiscard]] std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> GetClips() const;

        [[nodiscard]] bool operator==(const SoundEventData&) const = default;
    };

    // The names a file and Forge give the types, timings and steal modes, in their enums' order
    [[nodiscard]] TRINITY_API std::string_view GetSoundContainerTypeKey(SoundContainerType type);
    [[nodiscard]] TRINITY_API std::optional<SoundContainerType> FindSoundContainerType(std::string_view key);
    [[nodiscard]] TRINITY_API std::string_view GetSoundTriggerTimingKey(SoundTriggerTiming timing);
    [[nodiscard]] TRINITY_API std::optional<SoundTriggerTiming> FindSoundTriggerTiming(std::string_view key);
    [[nodiscard]] TRINITY_API std::string_view GetSoundStealModeKey(SoundStealMode mode);
    [[nodiscard]] TRINITY_API std::optional<SoundStealMode> FindSoundStealMode(std::string_view key);

    // Empty for an event that keeps to its rules, and otherwise what it breaks: there is a root and it comes first, every other container's parent is a container before it that is not a Clip, no two containers share an ID, every type is known and every value a number, every Switch picks by a parameter the event has or by none, each child of a Switch has a case that is a label of that parameter, or none, every parameter has a name and an ID no other has, and labels that are each named once, and every value of the event is a number
    [[nodiscard]] TRINITY_API std::string ValidateSoundEvent(const SoundEventData& data);

    // A Sound Event as its file has it. The clips it names are not held by it, so whatever plays it holds them, as a scene loading does to wait for those that preload
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
        // A number of its own for each version loaded, so a reload is told from the version before, wherever it lies
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

    // YAML, the event's own values, its parameters, each with its labels, then its containers in their order, the root without a Parent, each with the values of its type, a weight under a Random and a case under a Switch
    [[nodiscard]] TRINITY_API std::string WriteSoundEvent(const SoundEventData& data);
    // A key that is missing keeps its default. A malformed key, an event that breaks its rules, or a newer format, refuses the file
    [[nodiscard]] TRINITY_API Expected<SoundEventData, std::string> ParseSoundEvent(std::string_view text);
}