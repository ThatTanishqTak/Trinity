#include "Trinity/Audio/AudioMixerData.hpp"

#include "Trinity/Asset/CookedText.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <utility>

namespace Trinity
{
    namespace
    {
        std::atomic<std::uint64_t> s_NextSerial{ 1 };

        // Unity's names, units and defaults where Unity has the effect, with Resonance and Q at a Butterworth's 0.7071, so a cutoff is at -3 dB
        constexpr std::array<AudioEffectParameter, 2> c_LowpassParameters{ { { "Cutoff", "Cutoff", "Hz", 10.0f, 22000.0f, 5000.0f },{ "Resonance", "Resonance", "", 0.1f, 10.0f, 0.7071f } } };
        constexpr std::array<AudioEffectParameter, 2> c_HighpassParameters{ { { "Cutoff", "Cutoff", "Hz", 10.0f, 22000.0f, 5000.0f },{ "Resonance", "Resonance", "", 0.1f, 10.0f, 0.7071f } } };
        constexpr std::array<AudioEffectParameter, 2> c_BandParameters{ { { "Frequency", "Frequency", "Hz", 20.0f, 22000.0f, 1000.0f },{ "Q", "Q", "", 0.1f, 10.0f, 0.7071f } } };
        constexpr std::array<AudioEffectParameter, 3> c_ParamEQParameters{ { { "Center Frequency", "CenterFrequency", "Hz", 20.0f, 22000.0f, 8000.0f },{ "Octave Range", "OctaveRange", "octaves", 0.2f, 5.0f, 1.0f },{ "Frequency Gain", "FrequencyGain", "x", 0.05f, 3.0f, 1.0f } } };
        constexpr std::array<AudioEffectParameter, 3> c_LowShelfParameters{ { { "Frequency", "Frequency", "Hz", 20.0f, 22000.0f, 250.0f },{ "Gain", "Gain", "dB", -24.0f, 24.0f, 0.0f },{ "Slope", "Slope", "", 0.1f, 1.0f, 1.0f } } };
        constexpr std::array<AudioEffectParameter, 3> c_HighShelfParameters{ { { "Frequency", "Frequency", "Hz", 20.0f, 22000.0f, 5000.0f },{ "Gain", "Gain", "dB", -24.0f, 24.0f, 0.0f },{ "Slope", "Slope", "", 0.1f, 1.0f, 1.0f } } };
        constexpr std::array<AudioEffectParameter, 2> c_EchoParameters{ { { "Delay", "Delay", "ms", 10.0f, 5000.0f, 500.0f },{ "Decay", "Decay", "%", 0.0f, 100.0f, 50.0f } } };
        constexpr std::array<AudioEffectParameter, 5> c_CompressorParameters{ { { "Threshold", "Threshold", "dB", -60.0f, 0.0f, 0.0f },{ "Ratio", "Ratio", ":1", 1.0f, 50.0f, 2.5f },{ "Attack", "Attack", "ms", 0.1f, 500.0f, 50.0f },{ "Release", "Release", "ms", 10.0f, 5000.0f, 50.0f },{ "Make Up Gain", "MakeUpGain", "dB", 0.0f, 30.0f, 0.0f } } };
        constexpr std::array<AudioEffectParameter, 3> c_LimiterParameters{ { { "Ceiling", "Ceiling", "dB", -12.0f, 0.0f, 0.0f },{ "Release", "Release", "ms", 1.0f, 1000.0f, 10.0f },{ "Gain", "Gain", "dB", 0.0f, 12.0f, 0.0f } } };

        constexpr std::array<AudioEffectInfo, 11> c_EffectInfos{ {
            { AudioEffectType::Attenuation, "Attenuation", "Attenuation",{} },
            { AudioEffectType::Lowpass, "Lowpass", "Lowpass", c_LowpassParameters },
            { AudioEffectType::Highpass, "Highpass", "Highpass", c_HighpassParameters },
            { AudioEffectType::Bandpass, "Bandpass", "Bandpass", c_BandParameters },
            { AudioEffectType::Notch, "Notch", "Notch", c_BandParameters },
            { AudioEffectType::ParamEQ, "ParamEQ", "ParamEQ", c_ParamEQParameters },
            { AudioEffectType::LowShelf, "Low Shelf", "LowShelf", c_LowShelfParameters },
            { AudioEffectType::HighShelf, "High Shelf", "HighShelf", c_HighShelfParameters },
            { AudioEffectType::Echo, "Echo", "Echo", c_EchoParameters },
            { AudioEffectType::Compressor, "Compressor", "Compressor", c_CompressorParameters },
            { AudioEffectType::Limiter, "Limiter", "Limiter", c_LimiterParameters }
        } };

        static_assert(std::ranges::all_of(c_EffectInfos, [](const AudioEffectInfo& info) { return info.Parameters.size() <= AudioEffect::c_MaxParameters; }), "An effect has no more values than AudioEffect holds.");

        // An effect as a file gives it, at its type's defaults where a key is missing. Attenuation reads its type alone
        std::string ReadEffect(const YAML::Node& node, std::string_view group, AudioEffect& effect)
        {
            std::string l_Type;
            std::string l_Malformed;
            if (node.IsMap())
            {
                CookedText::ReadString(node, "Type", l_Type, l_Malformed);
            }

            const std::optional<AudioEffectType> l_Found = node.IsMap() && l_Malformed.empty() ? FindAudioEffectType(l_Type) : std::nullopt;
            if (!l_Found)
            {
                return std::format("group '{}' has an effect of no known type{}", group, l_Type.empty() ? std::string() : std::format(", '{}'", l_Type));
            }

            effect = MakeAudioEffect(*l_Found);
            if (effect.Type == AudioEffectType::Attenuation)
            {
                return {};
            }

            effect.ID = UUID();
            CookedText::ReadUUID(node, "ID", effect.ID, l_Malformed);
            CookedText::ReadBool(node, "Bypass", effect.Bypass, l_Malformed);
            CookedText::ReadNumber(node, "Wet", effect.Wet, l_Malformed);
            const std::span<const AudioEffectParameter> l_Parameters = GetAudioEffectInfo(effect.Type).Parameters;
            for (std::size_t it_Parameter = 0; it_Parameter < l_Parameters.size(); ++it_Parameter)
            {
                CookedText::ReadNumber(node, l_Parameters[it_Parameter].Key, effect.Parameters[it_Parameter], l_Malformed);
            }

            if (!l_Malformed.empty())
            {
                return std::format("group '{}': {} of a {} effect is malformed", group, l_Malformed, l_Type);
            }

            return {};
        }

        // A group as a file gives it, each key that is missing at its default. One without an ID is refused
        std::string ReadGroup(const YAML::Node& node, AudioMixerGroup& group)
        {
            if (!node.IsMap())
            {
                return "a group is not a map";
            }

            std::string l_Name;
            std::string l_Malformed;
            CookedText::ReadUUID(node, "ID", group.ID, l_Malformed);
            CookedText::ReadUUID(node, "Parent", group.Parent, l_Malformed);
            CookedText::ReadString(node, "Name", l_Name, l_Malformed);
            CookedText::ReadNumber(node, "Volume", group.Volume, l_Malformed);
            CookedText::ReadNumber(node, "Pitch", group.Pitch, l_Malformed);
            CookedText::ReadBool(node, "Mute", group.Mute, l_Malformed);
            CookedText::ReadBool(node, "Solo", group.Solo, l_Malformed);
            CookedText::ReadBool(node, "Bypass", group.Bypass, l_Malformed);
            if (!l_Malformed.empty())
            {
                return std::format("group '{}': {} is malformed", l_Name, l_Malformed);
            }

            group.Name = TaggedString<MemoryTag::Audio>(std::string_view(l_Name));
            if (!group.ID)
            {
                return std::format("group '{}' has no ID", l_Name);
            }

            const YAML::Node l_Effects = node["Effects"];
            if (l_Effects && !l_Effects.IsSequence())
            {
                return std::format("group '{}': Effects is not a list", l_Name);
            }

            for (std::size_t it_Effect = 0; l_Effects && it_Effect < l_Effects.size(); ++it_Effect)
            {
                if (std::string l_Error = ReadEffect(l_Effects[it_Effect], l_Name, group.Effects.emplace_back()); !l_Error.empty())
                {
                    return l_Error;
                }
            }

            return {};
        }
    }

    AudioMixerData AudioMixerData::MakeDefault()
    {
        AudioMixerData l_Data;
        AudioMixerGroup& l_Master = l_Data.Groups.emplace_back();
        l_Master.ID = UUID::Generate();
        l_Master.Name = TaggedString<MemoryTag::Audio>(std::string_view("Master"));

        return l_Data;
    }

    const AudioMixerGroup& AudioMixerData::GetMaster() const
    {
        return Groups.front();
    }

    const AudioMixerGroup* AudioMixerData::FindGroup(UUID id) const
    {
        const auto a_Found = std::ranges::find(Groups, id, &AudioMixerGroup::ID);

        return id && a_Found != Groups.end() ? &*a_Found : nullptr;
    }

    AudioMixerGroup* AudioMixerData::FindGroup(UUID id)
    {
        return const_cast<AudioMixerGroup*>(std::as_const(*this).FindGroup(id));
    }

    std::span<const AudioEffectInfo> GetAudioEffectInfos()
    {
        return c_EffectInfos;
    }

    const AudioEffectInfo& GetAudioEffectInfo(AudioEffectType type)
    {
        const std::size_t l_Index = static_cast<std::size_t>(type);

        return c_EffectInfos[l_Index < c_EffectInfos.size() ? l_Index : 0];
    }

    std::optional<AudioEffectType> FindAudioEffectType(std::string_view key)
    {
        const auto a_Found = std::ranges::find(c_EffectInfos, key, &AudioEffectInfo::Key);

        return a_Found != c_EffectInfos.end() ? std::optional(a_Found->Type) : std::nullopt;
    }

    AudioEffect MakeAudioEffect(AudioEffectType type)
    {
        AudioEffect l_Effect;
        l_Effect.Type = type;
        l_Effect.ID = type == AudioEffectType::Attenuation ? UUID() : UUID::Generate();
        const std::span<const AudioEffectParameter> l_Parameters = GetAudioEffectInfo(type).Parameters;
        for (std::size_t it_Parameter = 0; it_Parameter < l_Parameters.size(); ++it_Parameter)
        {
            l_Effect.Parameters[it_Parameter] = l_Parameters[it_Parameter].Default;
        }

        return l_Effect;
    }

    float AudioDecibelsToGain(float decibels)
    {
        return decibels > AudioMixerData::c_MinimumVolume ? std::pow(10.0f, std::min(decibels, AudioMixerData::c_MaximumVolume) / 20.0f) : 0.0f;
    }

    float AudioGainToDecibels(float gain)
    {
        return gain > 0.0f ? std::clamp(20.0f * std::log10(gain), AudioMixerData::c_MinimumVolume, AudioMixerData::c_MaximumVolume) : AudioMixerData::c_MinimumVolume;
    }

    // A group's parent must come before it, so a mixer has no group above itself
    std::string ValidateAudioMixer(const AudioMixerData& data)
    {
        if (data.Groups.empty() || data.Groups.front().Parent)
        {
            return "it has no Master, a group without a Parent, first";
        }

        for (std::size_t it_Group = 0; it_Group < data.Groups.size(); ++it_Group)
        {
            const AudioMixerGroup& l_Group = data.Groups[it_Group];
            const auto a_Before = data.Groups.begin() + static_cast<std::ptrdiff_t>(it_Group);
            if (!l_Group.ID || std::ranges::find(data.Groups.begin(), a_Before, l_Group.ID, &AudioMixerGroup::ID) != a_Before)
            {
                return std::format("group '{}' has no ID of its own", std::string_view(l_Group.Name));
            }

            if (it_Group > 0 && std::ranges::find(data.Groups.begin(), a_Before, l_Group.Parent, &AudioMixerGroup::ID) == a_Before)
            {
                return std::format("group '{}' is not after a parent of the mixer's", std::string_view(l_Group.Name));
            }

            if (!std::isfinite(l_Group.Volume) || !std::isfinite(l_Group.Pitch))
            {
                return std::format("group '{}' has a volume or pitch that is no number", std::string_view(l_Group.Name));
            }

            if (std::ranges::count(l_Group.Effects, AudioEffectType::Attenuation, &AudioEffect::Type) > 1)
            {
                return std::format("group '{}' has more than one Attenuation", std::string_view(l_Group.Name));
            }

            for (const AudioEffect& it_Effect : l_Group.Effects)
            {
                if (static_cast<std::size_t>(it_Effect.Type) >= c_EffectInfos.size())
                {
                    return std::format("group '{}' has an effect of no known type", std::string_view(l_Group.Name));
                }

                if (!std::isfinite(it_Effect.Wet) || !std::ranges::all_of(it_Effect.Parameters, [](float value) { return std::isfinite(value); }))
                {
                    return std::format("group '{}' has a {} whose values are not all numbers", std::string_view(l_Group.Name), GetAudioEffectInfo(it_Effect.Type).Name);
                }
            }
        }

        // Across the mixer, since a Send in one group names a Receive in another by its ID
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> l_EffectIDs;
        for (const AudioMixerGroup& it_Group : data.Groups)
        {
            for (const AudioEffect& it_Effect : it_Group.Effects)
            {
                if (it_Effect.Type == AudioEffectType::Attenuation)
                {
                    continue;
                }

                if (!it_Effect.ID || std::ranges::find(l_EffectIDs, it_Effect.ID) != l_EffectIDs.end())
                {
                    return std::format("a {} in group '{}' has no ID of its own", GetAudioEffectInfo(it_Effect.Type).Name, std::string_view(it_Group.Name));
                }

                l_EffectIDs.push_back(it_Effect.ID);
            }
        }

        return {};
    }

    AudioMixerAsset::AudioMixerAsset(AudioMixerData data) : m_Data(std::move(data)), m_Serial(s_NextSerial.fetch_add(1, std::memory_order_relaxed))
    {

    }

    Expected<Asset*, std::string> AudioMixerLoader::Load(std::span<const std::byte> data) const
    {
        Expected<AudioMixerData, std::string> l_Data = ParseAudioMixer(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
        if (!l_Data)
        {
            return Unexpected{ l_Data.GetError() };
        }

        return Memory::New<AudioMixerAsset>(MemoryTag::Audio, std::move(*l_Data));
    }

    std::string WriteAudioMixer(const AudioMixerData& data)
    {
        std::string l_Text = std::format("Format: {}", AudioMixerData::c_FormatVersion);
        ComponentWriter l_Writer(l_Text, 0);
        l_Writer.BeginList("Groups");
        for (const AudioMixerGroup& it_Group : data.Groups)
        {
            l_Writer.BeginItem();
            l_Writer.Write("ID", it_Group.ID);
            l_Writer.Write("Name", std::string_view(it_Group.Name));
            if (it_Group.Parent)
            {
                l_Writer.Write("Parent", it_Group.Parent);
            }

            l_Writer.Write("Volume", it_Group.Volume);
            l_Writer.Write("Pitch", it_Group.Pitch);
            l_Writer.Write("Mute", it_Group.Mute);
            l_Writer.Write("Solo", it_Group.Solo);
            l_Writer.Write("Bypass", it_Group.Bypass);
            if (it_Group.Effects.empty())
            {
                continue;
            }

            l_Writer.BeginList("Effects");
            for (const AudioEffect& it_Effect : it_Group.Effects)
            {
                const AudioEffectInfo& l_Info = GetAudioEffectInfo(it_Effect.Type);
                l_Writer.BeginItem();
                l_Writer.Write("Type", l_Info.Key);
                if (it_Effect.Type == AudioEffectType::Attenuation)
                {
                    continue;
                }

                l_Writer.Write("ID", it_Effect.ID);
                l_Writer.Write("Bypass", it_Effect.Bypass);
                l_Writer.Write("Wet", it_Effect.Wet);
                for (std::size_t it_Parameter = 0; it_Parameter < l_Info.Parameters.size(); ++it_Parameter)
                {
                    l_Writer.Write(l_Info.Parameters[it_Parameter].Key, it_Effect.Parameters[it_Parameter]);
                }
            }

            l_Writer.End();
        }

        l_Writer.End();

        return l_Text + "\n";
    }

    Expected<AudioMixerData, std::string> ParseAudioMixer(std::string_view text)
    {
        const Expected<YAML::Node, std::string> l_Root = CookedText::LoadRoot(text, "an audio mixer file", AudioMixerData::c_FormatVersion);
        if (!l_Root)
        {
            return Unexpected{ l_Root.GetError() };
        }

        const YAML::Node l_Groups = (*l_Root)["Groups"];
        if (!l_Groups || !l_Groups.IsSequence())
        {
            return Unexpected{ std::string("Groups is not a list") };
        }

        AudioMixerData l_Data;
        for (std::size_t it_Group = 0; it_Group < l_Groups.size(); ++it_Group)
        {
            if (std::string l_Error = ReadGroup(l_Groups[it_Group], l_Data.Groups.emplace_back()); !l_Error.empty())
            {
                return Unexpected{ std::move(l_Error) };
            }
        }

        if (std::string l_Error = ValidateAudioMixer(l_Data); !l_Error.empty())
        {
            return Unexpected{ std::move(l_Error) };
        }

        return l_Data;
    }
}