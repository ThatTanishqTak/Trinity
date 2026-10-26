#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // One group of an Audio Mixer, as Unity's AudioMixerGroup: the sounds played into it and the groups below it mixed together, then played at its pitch, which changes their speed with it as a tape's does, and its volume in decibels, where the minimum is silence. Mute silences it and everything below it. While any group is soloed only the soloed groups are heard, with the groups below them and the groups above that carry them, and a group that only carries a soloed one passes on nothing played into it directly. Bypass passes its effects by, once groups have them
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

    // Empty for a mixer that keeps to its rules, and otherwise what it breaks: there is a Master and it comes first, every other group's parent is a group before it, no two groups share an ID, and every volume and pitch is a number
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

    // YAML, the groups in their order, Master without a Parent
    [[nodiscard]] TRINITY_API std::string WriteAudioMixer(const AudioMixerData& data);
    // A key that is missing keeps its default. A malformed one, a mixer that breaks its rules, or a newer format, refuses the file
    [[nodiscard]] TRINITY_API Expected<AudioMixerData, std::string> ParseAudioMixer(std::string_view text);
}