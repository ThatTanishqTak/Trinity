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
    // A unit of a group's chain, by Unity's names. Attenuation is where the group's volume applies, so what comes before it is before the fader and what comes after it after
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
        Limiter
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

    // An effect on a group: its values in the order of its type's parameters, the rest 0, its wet mix in percent, which crosses from what comes into it at 0 to what it makes at 100, and Bypass, which passes it by. Each keeps its ID through edits, so a playing effect keeps its state, its echoes among it. Attenuation has neither, nor an ID, since it is the group's volume
    struct AudioEffect
    {
        static constexpr std::size_t c_MaxParameters = 16;

        UUID ID;
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

    // What a .trmixer file holds, Unity's Audio Mixer: a tree of groups from Master, which comes first, each group after its parent
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

        // Master alone at 0 dB, as a project without a mixer is heard
        [[nodiscard]] static AudioMixerData MakeDefault();

        // The first group, which a mixer that keeps to its rules has
        [[nodiscard]] const AudioMixerGroup& GetMaster() const;
        [[nodiscard]] const AudioMixerGroup* FindGroup(UUID id) const;
        [[nodiscard]] AudioMixerGroup* FindGroup(UUID id);

        [[nodiscard]] bool operator==(const AudioMixerData&) const = default;
    };

    // Linear, 0 at the minimum volume and below, and the other way, the minimum for 0
    [[nodiscard]] TRINITY_API float AudioDecibelsToGain(float decibels);
    [[nodiscard]] TRINITY_API float AudioGainToDecibels(float gain);

    // Empty for a mixer that keeps to its rules, and otherwise what it breaks: there is a Master and it comes first, every other group's parent is a group before it, no two groups share an ID, every volume and pitch is a number, no group has more than one Attenuation, every other effect has an ID no other effect has, and every effect's type is known and its values are numbers
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

    // YAML, the groups in their order, Master without a Parent, each with its effects in their order, unless it has none
    [[nodiscard]] TRINITY_API std::string WriteAudioMixer(const AudioMixerData& data);
    // A key that is missing keeps its default. A malformed one, a mixer that breaks its rules, or a newer format, refuses the file
    [[nodiscard]] TRINITY_API Expected<AudioMixerData, std::string> ParseAudioMixer(std::string_view text);
}