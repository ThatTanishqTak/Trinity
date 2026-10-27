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

    // Whether a parameter is a value along a range, as an engine's RPM, which seeks towards each value it is given, or one of its labels, as the surface a footstep falls on, which it jumps to and a Switch picks by
    enum class SoundParameterKind : std::uint8_t
    {
        Discrete,
        Continuous
    };

    // What a curve moves of its container, and of everything below it: the volume, in decibels added to the container's, the pitch, a speed the container's is multiplied by, or the cutoff of a low-pass, in hertz
    enum class SoundCurveTarget : std::uint8_t
    {
        Volume,
        Pitch,
        Cutoff
    };

    // A parameter mapped onto one of a container's values as it plays, as FMOD's automation is: straight lines between points, each a value of the parameter and the container's value there, in order of the parameter's value, and level beyond the first and the last. A container's curves on one value combine with each other and with those above it: volumes add, pitches multiply and the lowest cutoff wins
    struct TRINITY_API SoundCurve
    {
        UUID Parameter;
        SoundCurveTarget Target = SoundCurveTarget::Volume;
        std::vector<glm::vec2, TaggedAllocator<glm::vec2, MemoryTag::Audio>> Points;

        // At the parameter's value. 0 for a curve with no points
        [[nodiscard]] float Evaluate(float value) const;

        [[nodiscard]] bool operator==(const SoundCurve&) const = default;
    };

    // One container of a sound event's tree. Each has a volume in decibels and a pitch, a speed of playing as a source's is, each varied at random by up to its spread either way each time it is triggered, and a delay in seconds before it starts, lengthened at random by up to its own spread, and curves that move its volume, pitch and low-pass cutoff by the event's parameters as it plays. What else it holds is its type's: a Clip its clip, which loops while Loop is on, a Random how many of its last picks it never picks again, and whether it triggers itself, on an interval between two lengths, a number of times or for as long as the event plays, a Sequence whether it starts again from its first child once its last ends, and a Switch the parameter it picks by. A child of a Random has a weight, its share of the picks, and a child of a Switch a case, one of the parameter's labels, or none for every label no other child has. A value outside its range is played at its nearest end
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

    // A value each playing instance of a sound event is given by name, starting at its default, or for a global one the value the game gives every instance by that name, which the default stands for until the game has given one. A discrete one is one of its labels, by its index from 0, which its Switches pick by, and it jumps to each value it is given. A continuous one is along its range, and seeks towards each value at its speed, in its own units a second, or at once for 0, as FMOD's do. Curves map either onto its containers' values
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
        // A value as it plays: within its range, a discrete one's at its nearest label, and one that is no number at its default
        [[nodiscard]] float Clamp(float value) const;

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
        // A low-pass curve's cutoff goes from here to where it is all but clear
        static constexpr float c_MinimumCutoff = 10.0f;
        static constexpr float c_MaximumCutoff = 22000.0f;
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
        [[nodiscard]] SoundParameter* FindParameter(UUID id);
        [[nodiscard]] const SoundParameter* FindParameter(std::string_view name) const;
        // The clip of each Clip container that names one, once each, in the containers' order
        [[nodiscard]] std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> GetClips() const;
        [[nodiscard]] const SoundCurve* FindCurve(UUID container, UUID parameter, SoundCurveTarget target) const;
        [[nodiscard]] SoundCurve* FindCurve(UUID container, UUID parameter, SoundCurveTarget target);

        // The edits Forge's Sound Event panel makes, each of which keeps an event to its rules. A new container of the type below the parent, after everything below it already, with an ID of its own, at its defaults, a Clip playing the clip given. Null for a parent the event lacks or a Clip, which holds none
        SoundContainer* AddContainer(UUID parent, SoundContainerType type, std::string_view name, UUID clip = UUID());
        // A copy of the container and everything below it, the copy named as given, right after the last of them, below the same parent, each copy with an ID of its own. Null for the root or a container the event lacks
        SoundContainer* DuplicateContainer(UUID container, std::string_view name);
        // The container and everything below it. False for the root or a container the event lacks
        bool RemoveContainer(UUID container);
        // The container, with everything below it, below another parent, before one of the parent's children, or after the last when it names none. Its case is kept only below a Switch by the same parameter, and its weight only below a Random. False for the root, a container or a parent the event lacks, a Clip, a parent that is the container or below it, or a child before which is not the parent's
        bool MoveContainer(UUID container, UUID parent, UUID before = UUID());
        // Its children's cases are let go once it is no Switch, and their weights once it is no Random, as its parameter is once it is a Switch by a parameter that is not discrete. What the new type never saves, such as a Clip's clip or a Random's triggering, goes back to its default. False for a container the event lacks, or a Clip asked of one with children
        bool SetContainerType(UUID container, SoundContainerType type);
        // A Switch by a discrete parameter of the event's, or by none, its children's cases let go when it changes. False for a container that is no Switch, or a parameter that is not one of the event's discrete ones
        bool SetSwitchParameter(UUID container, UUID parameter);
        // At its kind's defaults: a continuous one from 0 to 1, and a discrete one with one label. Null for a name that is empty or another parameter's
        SoundParameter* AddParameter(std::string_view name, SoundParameterKind kind);
        // The parameter, with its curves, and the Switches by it, which pick by none, their children's cases let go. False for one the event lacks
        bool RemoveParameter(UUID parameter);
        // False for one the event lacks, or a name that is empty or another parameter's
        bool RenameParameter(UUID parameter, std::string_view name);
        // Continuous, its labels go, the Switches by it pick by none, and its range spans what the labels did. Discrete, it has one label. Its default is kept within its range. False for one the event lacks
        bool SetParameterKind(UUID parameter, SoundParameterKind kind);
        // A label last, or renamed, a case of it renamed with it, or removed, a case of it let go and a default past the last label kept to it. False for a parameter that is not one of the event's discrete ones, a name that is empty or another of its labels', a place beyond its labels, or its only label
        bool AddLabel(UUID parameter, std::string_view name);
        bool RenameLabel(UUID parameter, std::size_t index, std::string_view name);
        bool RemoveLabel(UUID parameter, std::size_t index);
        // A curve of the container's value by the parameter, level at the value that changes nothing across the parameter's range, or the one it has already. Null for a container or a parameter the event lacks
        SoundCurve* AddCurve(UUID container, UUID parameter, SoundCurveTarget target);
        // False for a curve the container lacks
        bool RemoveCurve(UUID container, UUID parameter, SoundCurveTarget target);


        [[nodiscard]] bool operator==(const SoundEventData&) const = default;
    };

    // The names a file and Forge give the types, timings, steal modes, parameter kinds and curve targets, in their enums' order
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

    // Empty for an event that keeps to its rules, and otherwise what it breaks: there is a root and it comes first, every other container's parent is a container before it that is not a Clip, no two containers share an ID, every type is known and every value a number, every Switch picks by a discrete parameter the event has or by none, each child of a Switch has a case that is a label of that parameter, or none, every parameter has a name and an ID no other has, a discrete one labels that are each named once and a continuous one none, a range from a minimum below its maximum and a seek speed of 0 or more, every curve is by a parameter the event has, of a known value, no other curve of its container being by that parameter on that value, with points in order of the parameter's value, no two at one value, and every value of the event is a number
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

    // YAML, the event's own values, its parameters, each with its kind's values, then its containers in their order, the root without a Parent, each with the values of its type, a weight under a Random, a case under a Switch and its curves
    [[nodiscard]] TRINITY_API std::string WriteSoundEvent(const SoundEventData& data);
    // A key that is missing keeps its default. A malformed key, an event that breaks its rules, or a newer format, refuses the file
    [[nodiscard]] TRINITY_API Expected<SoundEventData, std::string> ParseSoundEvent(std::string_view text);
}