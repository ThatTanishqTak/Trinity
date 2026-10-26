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