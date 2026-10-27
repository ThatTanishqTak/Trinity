#include "Trinity/Audio/SoundEventData.hpp"

#include "Trinity/Asset/CookedText.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>
#include <initializer_list>
#include <utility>

namespace Trinity
{
    namespace
    {
        std::atomic<std::uint64_t> s_NextSerial{ 1 };

        constexpr std::array<std::string_view, 5> c_ContainerTypeKeys{ "Clip", "Random", "Sequence", "Layer", "Switch" };
        constexpr std::array<std::string_view, 2> c_TriggerTimingKeys{ "Pulse", "Offset" };
        constexpr std::array<std::string_view, 3> c_StealModeKeys{ "Oldest", "Quietest", "None" };

        template<typename T, std::size_t N>
        std::string_view GetKey(const std::array<std::string_view, N>& keys, T value)
        {
            return static_cast<std::size_t>(value) < keys.size() ? keys[static_cast<std::size_t>(value)] : keys.front();
        }

        template<typename T, std::size_t N>
        std::optional<T> FindKey(const std::array<std::string_view, N>& keys, std::string_view key)
        {
            const auto a_Found = std::ranges::find(keys, key);

            return a_Found != keys.end() ? std::optional(static_cast<T>(a_Found - keys.begin())) : std::nullopt;
        }

        // By its name, or its ID when it has none, as an error names it
        std::string Describe(const SoundContainer& container)
        {
            return container.Name.empty() ? std::format("container {}", container.ID) : std::format("container '{}'", std::string_view(container.Name));
        }

        bool AreNumbers(std::initializer_list<float> values)
        {
            return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
        }

        // A container as a file gives it, each key that is missing at its default. Every key is read whatever its type, which the file writes only those of
        std::string ReadContainer(const YAML::Node& node, SoundContainer& container)
        {
            if (!node.IsMap())
            {
                return "a container is not a map";
            }

            std::string l_Name;
            std::string l_Type;
            std::string l_Timing(GetSoundTriggerTimingKey(container.Timing));
            std::string l_Case;
            std::string l_Malformed;
            CookedText::ReadUUID(node, "ID", container.ID, l_Malformed);
            CookedText::ReadUUID(node, "Parent", container.Parent, l_Malformed);
            CookedText::ReadString(node, "Name", l_Name, l_Malformed);
            CookedText::ReadString(node, "Type", l_Type, l_Malformed);
            CookedText::ReadNumber(node, "Volume", container.Volume, l_Malformed);
            CookedText::ReadNumber(node, "VolumeRandom", container.VolumeRandom, l_Malformed);
            CookedText::ReadNumber(node, "Pitch", container.Pitch, l_Malformed);
            CookedText::ReadNumber(node, "PitchRandom", container.PitchRandom, l_Malformed);
            CookedText::ReadNumber(node, "Delay", container.Delay, l_Malformed);
            CookedText::ReadNumber(node, "DelayRandom", container.DelayRandom, l_Malformed);
            CookedText::ReadNumber(node, "Weight", container.Weight, l_Malformed);
            CookedText::ReadString(node, "Case", l_Case, l_Malformed);
            CookedText::ReadUUID(node, "Clip", container.Clip, l_Malformed);
            CookedText::ReadBool(node, "Loop", container.Loop, l_Malformed);
            CookedText::ReadNumber(node, "AvoidRepeat", container.AvoidRepeat, l_Malformed);
            CookedText::ReadBool(node, "AutoTrigger", container.AutoTrigger, l_Malformed);
            CookedText::ReadString(node, "Timing", l_Timing, l_Malformed);
            CookedText::ReadNumber(node, "IntervalMinimum", container.IntervalMinimum, l_Malformed);
            CookedText::ReadNumber(node, "IntervalMaximum", container.IntervalMaximum, l_Malformed);
            CookedText::ReadNumber(node, "TriggerCount", container.TriggerCount, l_Malformed);
            CookedText::ReadUUID(node, "Parameter", container.Parameter, l_Malformed);
            container.Name = TaggedString<MemoryTag::Audio>(std::string_view(l_Name));
            container.Case = TaggedString<MemoryTag::Audio>(std::string_view(l_Case));
            if (!l_Malformed.empty())
            {
                return std::format("{}: {} is malformed", Describe(container), l_Malformed);
            }

            const std::optional<SoundContainerType> l_FoundType = FindSoundContainerType(l_Type);
            if (!l_FoundType)
            {
                return std::format("{} is of no known type{}", Describe(container), l_Type.empty() ? std::string() : std::format(", '{}'", l_Type));
            }

            const std::optional<SoundTriggerTiming> l_FoundTiming = FindSoundTriggerTiming(l_Timing);
            if (!l_FoundTiming)
            {
                return std::format("{}: its Timing is '{}', which is no timing", Describe(container), l_Timing);
            }

            container.Type = *l_FoundType;
            container.Timing = *l_FoundTiming;

            return {};
        }

        // A parameter as a file gives it, with its labels in their order
        std::string ReadParameter(const YAML::Node& node, SoundParameter& parameter)
        {
            if (!node.IsMap())
            {
                return "a parameter is not a map";
            }

            std::string l_Name;
            std::string l_Malformed;
            CookedText::ReadUUID(node, "ID", parameter.ID, l_Malformed);
            CookedText::ReadString(node, "Name", l_Name, l_Malformed);
            CookedText::ReadNumber(node, "Default", parameter.Default, l_Malformed);
            parameter.Name = TaggedString<MemoryTag::Audio>(std::string_view(l_Name));
            const YAML::Node l_Labels = node["Labels"];
            if (!l_Malformed.empty() || (l_Labels && !l_Labels.IsSequence()))
            {
                return std::format("parameter '{}': {} is malformed", l_Name, l_Malformed.empty() ? std::string("Labels") : l_Malformed);
            }

            for (std::size_t it_Label = 0; l_Labels && it_Label < l_Labels.size(); ++it_Label)
            {
                std::string l_Label;
                if (l_Labels[it_Label].IsMap())
                {
                    CookedText::ReadString(l_Labels[it_Label], "Name", l_Label, l_Malformed);
                }

                if (!l_Labels[it_Label].IsMap() || !l_Malformed.empty())
                {
                    return std::format("parameter '{}': a label is malformed", l_Name);
                }

                parameter.Labels.emplace_back(std::string_view(l_Label));
            }

            return {};
        }

        std::string ValidateContainers(const SoundEventData& data)
        {
            if (data.Containers.empty() || data.Containers.front().Parent)
            {
                return "it has no root container, one without a Parent, first";
            }

            for (std::size_t it_Container = 0; it_Container < data.Containers.size(); ++it_Container)
            {
                const SoundContainer& l_Container = data.Containers[it_Container];
                const auto a_Before = data.Containers.begin() + static_cast<std::ptrdiff_t>(it_Container);
                if (!l_Container.ID || std::ranges::find(data.Containers.begin(), a_Before, l_Container.ID, &SoundContainer::ID) != a_Before)
                {
                    return std::format("{} has no ID of its own", Describe(l_Container));
                }

                if (it_Container > 0)
                {
                    const auto a_Parent = std::ranges::find(data.Containers.begin(), a_Before, l_Container.Parent, &SoundContainer::ID);
                    if (a_Parent == a_Before)
                    {
                        return std::format("{} is not after a parent of the event's", Describe(l_Container));
                    }

                    if (a_Parent->Type == SoundContainerType::Clip)
                    {
                        return std::format("{} is below {}, a Clip, which holds no containers", Describe(l_Container), Describe(*a_Parent));
                    }
                }

                if (static_cast<std::size_t>(l_Container.Type) >= c_ContainerTypeKeys.size() || static_cast<std::size_t>(l_Container.Timing) >= c_TriggerTimingKeys.size())
                {
                    return std::format("{} is of no known type or timing", Describe(l_Container));
                }

                if (!AreNumbers({ l_Container.Volume, l_Container.VolumeRandom, l_Container.Pitch, l_Container.PitchRandom, l_Container.Delay, l_Container.DelayRandom, l_Container.Weight, l_Container.IntervalMinimum, l_Container.IntervalMaximum }))
                {
                    return std::format("{} has a value that is no number", Describe(l_Container));
                }
            }

            return {};
        }

        std::string ValidateParameters(const SoundEventData& data)
        {
            for (std::size_t it_Parameter = 0; it_Parameter < data.Parameters.size(); ++it_Parameter)
            {
                const SoundParameter& l_Parameter = data.Parameters[it_Parameter];
                const std::string_view l_Name(l_Parameter.Name);
                const auto a_Before = data.Parameters.begin() + static_cast<std::ptrdiff_t>(it_Parameter);
                if (l_Name.empty())
                {
                    return "a parameter has no name";
                }

                if (!l_Parameter.ID || std::ranges::find(data.Parameters.begin(), a_Before, l_Parameter.ID, &SoundParameter::ID) != a_Before)
                {
                    return std::format("parameter '{}' has no ID of its own", l_Name);
                }

                if (std::ranges::any_of(data.Parameters.begin(), a_Before, [l_Name](const SoundParameter& other) { return std::string_view(other.Name) == l_Name; }))
                {
                    return std::format("two parameters are named '{}'", l_Name);
                }

                if (l_Parameter.Labels.empty())
                {
                    return std::format("parameter '{}' has no labels", l_Name);
                }

                for (auto it_Label = l_Parameter.Labels.begin(); it_Label != l_Parameter.Labels.end(); ++it_Label)
                {
                    if (it_Label->empty())
                    {
                        return std::format("parameter '{}' has a label with no name", l_Name);
                    }

                    if (std::find(l_Parameter.Labels.begin(), it_Label, *it_Label) != it_Label)
                    {
                        return std::format("parameter '{}' has two labels named '{}'", l_Name, std::string_view(*it_Label));
                    }
                }

                if (!std::isfinite(l_Parameter.Default))
                {
                    return std::format("parameter '{}' has a default that is no number", l_Name);
                }
            }

            return {};
        }

        // Once the containers and the parameters keep to their rules, as a Switch names one and a case another's label
        std::string ValidateSwitches(const SoundEventData& data)
        {
            for (const SoundContainer& it_Container : data.Containers)
            {
                if (it_Container.Type == SoundContainerType::Switch && it_Container.Parameter && data.FindParameter(it_Container.Parameter) == nullptr)
                {
                    return std::format("{} is a Switch by no parameter the event has", Describe(it_Container));
                }

                const SoundContainer* l_Parent = it_Container.Parent ? data.FindContainer(it_Container.Parent) : nullptr;
                if (it_Container.Case.empty() || l_Parent == nullptr || l_Parent->Type != SoundContainerType::Switch)
                {
                    continue;
                }

                const SoundParameter* l_Parameter = l_Parent->Parameter ? data.FindParameter(l_Parent->Parameter) : nullptr;
                if (l_Parameter == nullptr)
                {
                    return std::format("{} plays for '{}', and {} picks by no parameter", Describe(it_Container), std::string_view(it_Container.Case), Describe(*l_Parent));
                }

                if (std::ranges::find(l_Parameter->Labels, it_Container.Case) == l_Parameter->Labels.end())
                {
                    return std::format("{} plays for '{}', which is no label of parameter '{}'", Describe(it_Container), std::string_view(it_Container.Case), std::string_view(l_Parameter->Name));
                }
            }

            return {};
        }
    }

    SoundEventData SoundEventData::MakeDefault()
    {
        SoundEventData l_Data;
        SoundContainer& l_Root = l_Data.Containers.emplace_back();
        l_Root.ID = UUID::Generate();
        l_Root.Name = TaggedString<MemoryTag::Audio>(std::string_view("Root"));
        l_Root.Type = SoundContainerType::Random;

        return l_Data;
    }

    const SoundContainer& SoundEventData::GetRoot() const
    {
        return Containers.front();
    }

    const SoundContainer* SoundEventData::FindContainer(UUID id) const
    {
        const auto a_Found = id ? std::ranges::find(Containers, id, &SoundContainer::ID) : Containers.end();

        return a_Found != Containers.end() ? &*a_Found : nullptr;
    }

    SoundContainer* SoundEventData::FindContainer(UUID id)
    {
        return const_cast<SoundContainer*>(std::as_const(*this).FindContainer(id));
    }

    const SoundParameter* SoundEventData::FindParameter(UUID id) const
    {
        const auto a_Found = id ? std::ranges::find(Parameters, id, &SoundParameter::ID) : Parameters.end();

        return a_Found != Parameters.end() ? &*a_Found : nullptr;
    }

    const SoundParameter* SoundEventData::FindParameter(std::string_view name) const
    {
        const auto a_Found = std::ranges::find_if(Parameters, [name](const SoundParameter& parameter) { return std::string_view(parameter.Name) == name; });

        return a_Found != Parameters.end() ? &*a_Found : nullptr;
    }

    std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> SoundEventData::GetClips() const
    {
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> l_Clips;
        for (const SoundContainer& it_Container : Containers)
        {
            if (it_Container.Type == SoundContainerType::Clip && it_Container.Clip && std::ranges::find(l_Clips, it_Container.Clip) == l_Clips.end())
            {
                l_Clips.push_back(it_Container.Clip);
            }
        }

        return l_Clips;
    }

    std::string_view GetSoundContainerTypeKey(SoundContainerType type)
    {
        return GetKey(c_ContainerTypeKeys, type);
    }

    std::optional<SoundContainerType> FindSoundContainerType(std::string_view key)
    {
        return FindKey<SoundContainerType>(c_ContainerTypeKeys, key);
    }

    std::string_view GetSoundTriggerTimingKey(SoundTriggerTiming timing)
    {
        return GetKey(c_TriggerTimingKeys, timing);
    }

    std::optional<SoundTriggerTiming> FindSoundTriggerTiming(std::string_view key)
    {
        return FindKey<SoundTriggerTiming>(c_TriggerTimingKeys, key);
    }

    std::string_view GetSoundStealModeKey(SoundStealMode mode)
    {
        return GetKey(c_StealModeKeys, mode);
    }

    std::optional<SoundStealMode> FindSoundStealMode(std::string_view key)
    {
        return FindKey<SoundStealMode>(c_StealModeKeys, key);
    }

    std::string ValidateSoundEvent(const SoundEventData& data)
    {
        if (std::string l_Error = ValidateContainers(data); !l_Error.empty())
        {
            return l_Error;
        }

        if (std::string l_Error = ValidateParameters(data); !l_Error.empty())
        {
            return l_Error;
        }

        if (std::string l_Error = ValidateSwitches(data); !l_Error.empty())
        {
            return l_Error;
        }

        if (static_cast<std::size_t>(data.Steal) >= c_StealModeKeys.size() || static_cast<std::size_t>(data.Rolloff) >= c_AudioRolloffKeys.size())
        {
            return "it steals or rolls off in no known way";
        }

        if (!AreNumbers({ data.Cooldown, data.DopplerLevel, data.Spread, data.MinDistance, data.MaxDistance, data.OcclusionVolume, data.OcclusionCutoff }))
        {
            return "it has a value that is no number";
        }

        return {};
    }

    SoundEventAsset::SoundEventAsset(SoundEventData data) : m_Data(std::move(data)), m_Clips(m_Data.GetClips()), m_Serial(s_NextSerial.fetch_add(1, std::memory_order_relaxed))
    {

    }

    Expected<Asset*, std::string> SoundEventLoader::Load(std::span<const std::byte> data) const
    {
        Expected<SoundEventData, std::string> l_Data = ParseSoundEvent(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
        if (!l_Data)
        {
            return Unexpected{ l_Data.GetError() };
        }

        return Memory::New<SoundEventAsset>(MemoryTag::Audio, std::move(*l_Data));
    }

    std::string WriteSoundEvent(const SoundEventData& data)
    {
        std::string l_Text = std::format("Format: {}", SoundEventData::c_FormatVersion);
        ComponentWriter l_Writer(l_Text, 0);
        if (data.Output)
        {
            l_Writer.Write("Output", data.Output);
        }

        l_Writer.Write("Priority", data.Priority);
        l_Writer.Write("MaxInstances", data.MaxInstances);
        l_Writer.Write("Steal", GetSoundStealModeKey(data.Steal));
        l_Writer.Write("Cooldown", data.Cooldown);
        l_Writer.Write("Spatialize", data.Spatialize);
        l_Writer.Write("DopplerLevel", data.DopplerLevel);
        l_Writer.Write("Spread", data.Spread);
        l_Writer.Write("MinDistance", data.MinDistance);
        l_Writer.Write("MaxDistance", data.MaxDistance);
        l_Writer.Write("Rolloff", GetAudioRolloffKey(data.Rolloff));
        l_Writer.Write("Occlusion", data.Occlusion);
        l_Writer.Write("OcclusionVolume", data.OcclusionVolume);
        l_Writer.Write("OcclusionCutoff", data.OcclusionCutoff);
        if (!data.Parameters.empty())
        {
            l_Writer.BeginList("Parameters");
            for (const SoundParameter& it_Parameter : data.Parameters)
            {
                l_Writer.BeginItem();
                l_Writer.Write("ID", it_Parameter.ID);
                l_Writer.Write("Name", std::string_view(it_Parameter.Name));
                l_Writer.Write("Default", it_Parameter.Default);
                l_Writer.BeginList("Labels");
                for (const TaggedString<MemoryTag::Audio>& it_Label : it_Parameter.Labels)
                {
                    l_Writer.BeginItem();
                    l_Writer.Write("Name", std::string_view(it_Label));
                }

                l_Writer.End();
            }

            l_Writer.End();
        }

        l_Writer.BeginList("Containers");
        for (const SoundContainer& it_Container : data.Containers)
        {
            l_Writer.BeginItem();
            l_Writer.Write("ID", it_Container.ID);
            l_Writer.Write("Name", std::string_view(it_Container.Name));
            if (it_Container.Parent)
            {
                l_Writer.Write("Parent", it_Container.Parent);
            }

            l_Writer.Write("Type", GetSoundContainerTypeKey(it_Container.Type));
            l_Writer.Write("Volume", it_Container.Volume);
            l_Writer.Write("VolumeRandom", it_Container.VolumeRandom);
            l_Writer.Write("Pitch", it_Container.Pitch);
            l_Writer.Write("PitchRandom", it_Container.PitchRandom);
            l_Writer.Write("Delay", it_Container.Delay);
            l_Writer.Write("DelayRandom", it_Container.DelayRandom);
            const SoundContainer* l_Parent = it_Container.Parent ? data.FindContainer(it_Container.Parent) : nullptr;
            if (l_Parent != nullptr && l_Parent->Type == SoundContainerType::Random)
            {
                l_Writer.Write("Weight", it_Container.Weight);
            }

            if (l_Parent != nullptr && l_Parent->Type == SoundContainerType::Switch && !it_Container.Case.empty())
            {
                l_Writer.Write("Case", std::string_view(it_Container.Case));
            }

            switch (it_Container.Type)
            {
                case SoundContainerType::Clip:
                {
                    if (it_Container.Clip)
                    {
                        l_Writer.Write("Clip", it_Container.Clip);
                    }

                    l_Writer.Write("Loop", it_Container.Loop);
                    break;
                }
                case SoundContainerType::Random:
                {
                    l_Writer.Write("AvoidRepeat", it_Container.AvoidRepeat);
                    l_Writer.Write("AutoTrigger", it_Container.AutoTrigger);
                    l_Writer.Write("Timing", GetSoundTriggerTimingKey(it_Container.Timing));
                    l_Writer.Write("IntervalMinimum", it_Container.IntervalMinimum);
                    l_Writer.Write("IntervalMaximum", it_Container.IntervalMaximum);
                    l_Writer.Write("TriggerCount", it_Container.TriggerCount);
                    break;
                }
                case SoundContainerType::Sequence:
                {
                    l_Writer.Write("Loop", it_Container.Loop);
                    break;
                }
                case SoundContainerType::Switch:
                {
                    if (it_Container.Parameter)
                    {
                        l_Writer.Write("Parameter", it_Container.Parameter);
                    }

                    break;
                }
                case SoundContainerType::Layer:
                {
                    break;
                }
            }
        }

        l_Writer.End();

        return l_Text + "\n";
    }

    Expected<SoundEventData, std::string> ParseSoundEvent(std::string_view text)
    {
        const Expected<YAML::Node, std::string> l_Root = CookedText::LoadRoot(text, "a sound event file", SoundEventData::c_FormatVersion);
        if (!l_Root)
        {
            return Unexpected{ l_Root.GetError() };
        }

        SoundEventData l_Data;
        std::string l_Steal(GetSoundStealModeKey(l_Data.Steal));
        std::string l_Rolloff(GetAudioRolloffKey(l_Data.Rolloff));
        std::string l_Malformed;
        CookedText::ReadUUID(*l_Root, "Output", l_Data.Output, l_Malformed);
        CookedText::ReadNumber(*l_Root, "Priority", l_Data.Priority, l_Malformed);
        CookedText::ReadNumber(*l_Root, "MaxInstances", l_Data.MaxInstances, l_Malformed);
        CookedText::ReadString(*l_Root, "Steal", l_Steal, l_Malformed);
        CookedText::ReadNumber(*l_Root, "Cooldown", l_Data.Cooldown, l_Malformed);
        CookedText::ReadBool(*l_Root, "Spatialize", l_Data.Spatialize, l_Malformed);
        CookedText::ReadNumber(*l_Root, "DopplerLevel", l_Data.DopplerLevel, l_Malformed);
        CookedText::ReadNumber(*l_Root, "Spread", l_Data.Spread, l_Malformed);
        CookedText::ReadNumber(*l_Root, "MinDistance", l_Data.MinDistance, l_Malformed);
        CookedText::ReadNumber(*l_Root, "MaxDistance", l_Data.MaxDistance, l_Malformed);
        CookedText::ReadString(*l_Root, "Rolloff", l_Rolloff, l_Malformed);
        CookedText::ReadBool(*l_Root, "Occlusion", l_Data.Occlusion, l_Malformed);
        CookedText::ReadNumber(*l_Root, "OcclusionVolume", l_Data.OcclusionVolume, l_Malformed);
        CookedText::ReadNumber(*l_Root, "OcclusionCutoff", l_Data.OcclusionCutoff, l_Malformed);
        if (!l_Malformed.empty())
        {
            return Unexpected{ std::format("{} is malformed", l_Malformed) };
        }

        const std::optional<SoundStealMode> l_FoundSteal = FindSoundStealMode(l_Steal);
        const std::optional<AudioRolloff> l_FoundRolloff = FindAudioRolloff(l_Rolloff);
        if (!l_FoundSteal || !l_FoundRolloff)
        {
            return Unexpected{ !l_FoundSteal ? std::format("Steal is '{}', which is no steal mode", l_Steal) : std::format("Rolloff is '{}', which is no rolloff", l_Rolloff) };
        }

        l_Data.Steal = *l_FoundSteal;
        l_Data.Rolloff = *l_FoundRolloff;
        const YAML::Node l_Parameters = (*l_Root)["Parameters"];
        const YAML::Node l_Containers = (*l_Root)["Containers"];
        if ((l_Parameters && !l_Parameters.IsSequence()) || !l_Containers || !l_Containers.IsSequence())
        {
            return Unexpected{ std::string(l_Parameters && !l_Parameters.IsSequence() ? "Parameters is not a list" : "Containers is not a list") };
        }

        for (std::size_t it_Parameter = 0; l_Parameters && it_Parameter < l_Parameters.size(); ++it_Parameter)
        {
            if (std::string l_Error = ReadParameter(l_Parameters[it_Parameter], l_Data.Parameters.emplace_back()); !l_Error.empty())
            {
                return Unexpected{ std::move(l_Error) };
            }
        }

        for (std::size_t it_Container = 0; it_Container < l_Containers.size(); ++it_Container)
        {
            if (std::string l_Error = ReadContainer(l_Containers[it_Container], l_Data.Containers.emplace_back()); !l_Error.empty())
            {
                return Unexpected{ std::move(l_Error) };
            }
        }

        if (std::string l_Error = ValidateSoundEvent(l_Data); !l_Error.empty())
        {
            return Unexpected{ std::move(l_Error) };
        }

        return l_Data;
    }
}