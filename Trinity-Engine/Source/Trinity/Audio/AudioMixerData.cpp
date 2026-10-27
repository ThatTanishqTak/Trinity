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
        // Unity's SFX Reverb, its levels in millibels as Unity has them, -10000 being silence
        constexpr std::array<AudioEffectParameter, 14> c_ReverbParameters{ {
            { "Dry Level", "DryLevel", "mB", -10000.0f, 0.0f, 0.0f },
            { "Room", "Room", "mB", -10000.0f, 0.0f, 0.0f },
            { "Room HF", "RoomHF", "mB", -10000.0f, 0.0f, 0.0f },
            { "Decay Time", "DecayTime", "s", 0.1f, 20.0f, 1.0f },
            { "Decay HF Ratio", "DecayHFRatio", "", 0.1f, 2.0f, 0.5f },
            { "Reflections", "Reflections", "mB", -10000.0f, 1000.0f, -10000.0f },
            { "Reflect Delay", "ReflectDelay", "s", 0.0f, 0.3f, 0.02f },
            { "Reverb", "Reverb", "mB", -10000.0f, 2000.0f, 0.0f },
            { "Reverb Delay", "ReverbDelay", "s", 0.0f, 0.1f, 0.04f },
            { "Diffusion", "Diffusion", "%", 0.0f, 100.0f, 100.0f },
            { "Density", "Density", "%", 0.0f, 100.0f, 100.0f },
            { "HF Reference", "HFReference", "Hz", 20.0f, 20000.0f, 5000.0f },
            { "Room LF", "RoomLF", "mB", -10000.0f, 0.0f, 0.0f },
            { "LF Reference", "LFReference", "Hz", 20.0f, 1000.0f, 250.0f }
        } };
        // At -80 dB, as Unity's starts, a Send sends nothing until it is turned up
        constexpr std::array<AudioEffectParameter, 1> c_SendParameters{ { { "Send Level", "SendLevel", "dB", -80.0f, 20.0f, -80.0f } } };
        // Unity's Duck Volume, its ratio in percent, 200% being 2:1, and its attack and release the times its ducking takes to come and go 99% of the way
        constexpr std::array<AudioEffectParameter, 7> c_DuckParameters{ {
            { "Threshold", "Threshold", "dB", -100.0f, 0.0f, -10.0f },
            { "Ratio", "Ratio", "%", 100.0f, 5000.0f, 200.0f },
            { "Attack Time", "AttackTime", "ms", 0.0f, 5000.0f, 100.0f },
            { "Release Time", "ReleaseTime", "ms", 0.0f, 5000.0f, 100.0f },
            { "Make-up Gain", "MakeUpGain", "dB", -24.0f, 24.0f, 0.0f },
            { "Knee", "Knee", "dB", 0.0f, 50.0f, 10.0f },
            { "Sidechain Mix", "SidechainMix", "%", 0.0f, 100.0f, 100.0f }
        } };

        constexpr std::array<AudioEffectInfo, 15> c_EffectInfos{ {
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
            { AudioEffectType::Limiter, "Limiter", "Limiter", c_LimiterParameters },
            { AudioEffectType::SFXReverb, "SFX Reverb", "SFXReverb", c_ReverbParameters },
            { AudioEffectType::Send, "Send", "Send", c_SendParameters },
            { AudioEffectType::Receive, "Receive", "Receive",{} },
            { AudioEffectType::DuckVolume, "Duck Volume", "DuckVolume", c_DuckParameters }
        } };

        static_assert(std::ranges::all_of(c_EffectInfos, [](const AudioEffectInfo& info) { return info.Parameters.size() <= AudioEffect::c_MaxParameters; }), "An effect has no more values than AudioEffect holds.");

        // A group's values, and every effect's Wet, as snapshots hold them and names expose them
        constexpr std::array<AudioEffectParameter, 2> c_GroupValues{ { { "Volume", "Volume", "dB", AudioMixerData::c_MinimumVolume, AudioMixerData::c_MaximumVolume, 0.0f },{ "Pitch", "Pitch", "", AudioMixerData::c_MinimumPitch, AudioMixerData::c_MaximumPitch, 1.0f } } };
        constexpr AudioEffectParameter c_WetValue{ "Wet", "Wet", "%", 0.0f, 100.0f, 100.0f };
        constexpr std::array<std::string_view, 6> c_CurveKeys{ "Linear", "Smoothstep", "Squared", "SquareRoot", "BrickwallStart", "BrickwallEnd" };

        // The group or the effect an ID names, of which a mixer that keeps to its rules has one at most. Attenuation has no ID, so it is never one
        struct ValueOwner
        {
            const AudioMixerGroup* Group = nullptr;
            const AudioEffect* Effect = nullptr;
        };

        [[nodiscard]] ValueOwner FindOwner(const AudioMixerData& data, UUID id)
        {
            for (const AudioMixerGroup& it_Group : data.Groups)
            {
                if (!id)
                {
                    break;
                }

                if (it_Group.ID == id)
                {
                    return { &it_Group, nullptr };
                }

                for (const AudioEffect& it_Effect : it_Group.Effects)
                {
                    if (it_Effect.Type != AudioEffectType::Attenuation && it_Effect.ID == id)
                    {
                        return { nullptr, &it_Effect };
                    }
                }
            }

            return {};
        }

        [[nodiscard]] std::size_t CountSlots(const ValueOwner& owner)
        {
            return owner.Group != nullptr ? c_GroupValues.size() : (owner.Effect != nullptr ? GetAudioEffectInfo(owner.Effect->Type).Parameters.size() + 1 : 0);
        }

        // The group's or the effect's own, which is the start snapshot's
        [[nodiscard]] float ReadOwnValue(const ValueOwner& owner, std::uint8_t slot)
        {
            if (owner.Group != nullptr)
            {
                return slot == AudioMixerValue::c_Volume ? owner.Group->Volume : owner.Group->Pitch;
            }

            return slot == AudioMixerValue::c_Wet ? owner.Effect->Wet : owner.Effect->Parameters[slot - 1];
        }

        // Each group in its order, then its effects in theirs, Attenuation aside, which is the order a file gives a snapshot's values in
        template<typename Function>
        void ForEachOwner(const AudioMixerData& data, Function function)
        {
            for (const AudioMixerGroup& it_Group : data.Groups)
            {
                function(it_Group.ID);
                for (const AudioEffect& it_Effect : it_Group.Effects)
                {
                    if (it_Effect.Type != AudioEffectType::Attenuation)
                    {
                        function(it_Effect.ID);
                    }
                }
            }
        }

        // The ID of a list's item, and the group or effect it names, which a mixer that lacks it refuses
        std::string ReadOwner(const YAML::Node& node, const AudioMixerData& data, std::string_view what, UUID& owner)
        {
            std::string l_Malformed;
            if (node.IsMap())
            {
                CookedText::ReadUUID(node, "ID", owner, l_Malformed);
            }

            if (!node.IsMap() || !l_Malformed.empty() || data.GetSlotCount(owner) == 0)
            {
                return std::format("{} for no group or effect the mixer has", what);
            }

            return {};
        }

        // A snapshot as a file gives it, each value it leaves out at the start snapshot's and each curve at Linear
        std::string ReadSnapshot(const YAML::Node& node, const AudioMixerData& data, AudioMixerSnapshot& snapshot)
        {
            if (!node.IsMap())
            {
                return "a snapshot is not a map";
            }

            std::string l_Name;
            std::string l_Malformed;
            CookedText::ReadUUID(node, "ID", snapshot.ID, l_Malformed);
            CookedText::ReadString(node, "Name", l_Name, l_Malformed);
            if (!l_Malformed.empty())
            {
                return std::format("snapshot '{}': {} is malformed", l_Name, l_Malformed);
            }

            snapshot.Name = TaggedString<MemoryTag::Audio>(std::string_view(l_Name));
            const YAML::Node l_Values = node["Values"];
            const YAML::Node l_Curves = node["Curves"];
            if ((l_Values && !l_Values.IsSequence()) || (l_Curves && !l_Curves.IsSequence()))
            {
                return std::format("snapshot '{}': Values or Curves is not a list", l_Name);
            }

            for (std::size_t it_Item = 0; l_Values && it_Item < l_Values.size(); ++it_Item)
            {
                AudioSnapshotValues& l_Entry = snapshot.Values.emplace_back();
                if (std::string l_Error = ReadOwner(l_Values[it_Item], data, std::format("snapshot '{}' has values", l_Name), l_Entry.Owner); !l_Error.empty())
                {
                    return l_Error;
                }

                const ValueOwner l_Owner = FindOwner(data, l_Entry.Owner);
                for (std::uint8_t it_Slot = 0; it_Slot < CountSlots(l_Owner); ++it_Slot)
                {
                    l_Entry.Values[it_Slot] = ReadOwnValue(l_Owner, it_Slot);
                    CookedText::ReadNumber(l_Values[it_Item], data.GetValueInfo({ l_Entry.Owner, it_Slot })->Key, l_Entry.Values[it_Slot], l_Malformed);
                }
            }

            for (std::size_t it_Item = 0; l_Curves && it_Item < l_Curves.size(); ++it_Item)
            {
                AudioSnapshotCurves& l_Entry = snapshot.Curves.emplace_back();
                if (std::string l_Error = ReadOwner(l_Curves[it_Item], data, std::format("snapshot '{}' has curves", l_Name), l_Entry.Owner); !l_Error.empty())
                {
                    return l_Error;
                }

                for (std::uint8_t it_Slot = 0; it_Slot < data.GetSlotCount(l_Entry.Owner); ++it_Slot)
                {
                    const std::string_view l_Key = data.GetValueInfo({ l_Entry.Owner, it_Slot })->Key;
                    std::string l_Curve;
                    CookedText::ReadString(l_Curves[it_Item], l_Key, l_Curve, l_Malformed);
                    const std::optional<AudioTransitionCurve> l_Found = l_Curve.empty() ? std::optional(AudioTransitionCurve::Linear) : FindAudioTransitionCurve(l_Curve);
                    if (!l_Found)
                    {
                        return std::format("snapshot '{}': the curve for {} is '{}', which is no curve", l_Name, l_Key, l_Curve);
                    }

                    l_Entry.Curves[it_Slot] = *l_Found;
                }
            }

            if (!l_Malformed.empty())
            {
                return std::format("snapshot '{}': {} is malformed", l_Name, l_Malformed);
            }

            return {};
        }

        // An exposed value as a file gives it: its name, the ID of its group or effect, and which of its values by its key
        std::string ReadExposed(const YAML::Node& node, const AudioMixerData& data, AudioExposedParameter& exposed)
        {
            std::string l_Name;
            std::string l_Key;
            std::string l_Malformed;
            if (node.IsMap())
            {
                CookedText::ReadString(node, "Name", l_Name, l_Malformed);
                CookedText::ReadString(node, "Value", l_Key, l_Malformed);
            }

            if (std::string l_Error = ReadOwner(node, data, std::format("exposed value '{}' is a value", l_Name), exposed.Value.Owner); !l_Error.empty() || !l_Malformed.empty())
            {
                return l_Error.empty() ? std::format("exposed value '{}': {} is malformed", l_Name, l_Malformed) : l_Error;
            }

            exposed.Name = TaggedString<MemoryTag::Audio>(std::string_view(l_Name));
            for (std::uint8_t it_Slot = 0; it_Slot < data.GetSlotCount(exposed.Value.Owner); ++it_Slot)
            {
                if (data.GetValueInfo({ exposed.Value.Owner, it_Slot })->Key == l_Key)
                {
                    exposed.Value.Slot = it_Slot;

                    return {};
                }
            }

            return std::format("exposed value '{}' is '{}', which its group or effect has no value of", l_Name, l_Key);
        }

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
            if (effect.Type == AudioEffectType::Send)
            {
                CookedText::ReadUUID(node, "Target", effect.Target, l_Malformed);
            }

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
        AudioMixerSnapshot& l_Start = l_Data.Snapshots.emplace_back();
        l_Start.ID = UUID::Generate();
        l_Start.Name = TaggedString<MemoryTag::Audio>(std::string_view("Snapshot"));
        l_Data.StartSnapshot = l_Start.ID;

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

    const AudioMixerSnapshot& AudioMixerData::GetStartSnapshot() const
    {
        const AudioMixerSnapshot* l_Start = FindSnapshot(StartSnapshot);

        return l_Start != nullptr ? *l_Start : Snapshots.front();
    }

    const AudioMixerSnapshot* AudioMixerData::FindSnapshot(UUID id) const
    {
        const auto a_Found = std::ranges::find(Snapshots, id, &AudioMixerSnapshot::ID);

        return id && a_Found != Snapshots.end() ? &*a_Found : nullptr;
    }

    AudioMixerSnapshot* AudioMixerData::FindSnapshot(UUID id)
    {
        return const_cast<AudioMixerSnapshot*>(std::as_const(*this).FindSnapshot(id));
    }

    const AudioMixerSnapshot* AudioMixerData::FindSnapshot(std::string_view name) const
    {
        const auto a_Found = std::ranges::find_if(Snapshots, [name](const AudioMixerSnapshot& snapshot) { return std::string_view(snapshot.Name) == name; });

        return a_Found != Snapshots.end() ? &*a_Found : nullptr;
    }

    const AudioExposedParameter* AudioMixerData::FindExposed(std::string_view name) const
    {
        const auto a_Found = std::ranges::find_if(Exposed, [name](const AudioExposedParameter& exposed) { return std::string_view(exposed.Name) == name; });

        return a_Found != Exposed.end() ? &*a_Found : nullptr;
    }

    std::optional<AudioEffectParameter> AudioMixerData::GetValueInfo(AudioMixerValue value) const
    {
        const ValueOwner l_Owner = FindOwner(*this, value.Owner);
        if (value.Slot >= CountSlots(l_Owner))
        {
            return std::nullopt;
        }

        if (l_Owner.Group != nullptr)
        {
            return c_GroupValues[value.Slot];
        }

        return value.Slot == AudioMixerValue::c_Wet ? c_WetValue : GetAudioEffectInfo(l_Owner.Effect->Type).Parameters[value.Slot - 1];
    }

    std::size_t AudioMixerData::GetSlotCount(UUID owner) const
    {
        return CountSlots(FindOwner(*this, owner));
    }

    std::optional<float> AudioMixerData::GetValue(const AudioMixerSnapshot& snapshot, AudioMixerValue value) const
    {
        const ValueOwner l_Owner = FindOwner(*this, value.Owner);
        if (value.Slot >= CountSlots(l_Owner))
        {
            return std::nullopt;
        }

        const auto a_Own = std::ranges::find(snapshot.Values, value.Owner, &AudioSnapshotValues::Owner);

        return snapshot.ID != StartSnapshot && a_Own != snapshot.Values.end() ? a_Own->Values[value.Slot] : ReadOwnValue(l_Owner, value.Slot);
    }

    AudioTransitionCurve AudioMixerData::GetCurve(const AudioMixerSnapshot& snapshot, AudioMixerValue value) const
    {
        const auto a_Own = std::ranges::find(snapshot.Curves, value.Owner, &AudioSnapshotCurves::Owner);

        return a_Own != snapshot.Curves.end() && value.Slot < AudioMixerValue::c_MaxSlots ? a_Own->Curves[value.Slot] : AudioTransitionCurve::Linear;
    }

    // The start snapshot's value is its group's or its effect's own, which this data may change, as FindGroup lets it
    bool AudioMixerData::SetValue(UUID snapshot, AudioMixerValue value, float number)
    {
        AudioMixerSnapshot* l_Snapshot = FindSnapshot(snapshot);
        const ValueOwner l_Owner = FindOwner(*this, value.Owner);
        if (l_Snapshot == nullptr || value.Slot >= CountSlots(l_Owner))
        {
            return false;
        }

        if (snapshot == StartSnapshot && l_Owner.Group != nullptr)
        {
            AudioMixerGroup& l_Group = const_cast<AudioMixerGroup&>(*l_Owner.Group);
            (value.Slot == AudioMixerValue::c_Volume ? l_Group.Volume : l_Group.Pitch) = number;

            return true;
        }

        if (snapshot == StartSnapshot)
        {
            AudioEffect& l_Effect = const_cast<AudioEffect&>(*l_Owner.Effect);
            (value.Slot == AudioMixerValue::c_Wet ? l_Effect.Wet : l_Effect.Parameters[value.Slot - 1]) = number;

            return true;
        }

        auto a_Own = std::ranges::find(l_Snapshot->Values, value.Owner, &AudioSnapshotValues::Owner);
        if (a_Own == l_Snapshot->Values.end())
        {
            AudioSnapshotValues& l_Entry = l_Snapshot->Values.emplace_back();
            l_Entry.Owner = value.Owner;
            for (std::uint8_t it_Slot = 0; it_Slot < CountSlots(l_Owner); ++it_Slot)
            {
                l_Entry.Values[it_Slot] = ReadOwnValue(l_Owner, it_Slot);
            }

            a_Own = l_Snapshot->Values.end() - 1;
        }

        a_Own->Values[value.Slot] = number;

        return true;
    }

    bool AudioMixerData::SetCurve(UUID snapshot, AudioMixerValue value, AudioTransitionCurve curve)
    {
        AudioMixerSnapshot* l_Snapshot = FindSnapshot(snapshot);
        if (l_Snapshot == nullptr || value.Slot >= GetSlotCount(value.Owner))
        {
            return false;
        }

        auto a_Own = std::ranges::find(l_Snapshot->Curves, value.Owner, &AudioSnapshotCurves::Owner);
        if (a_Own == l_Snapshot->Curves.end())
        {
            l_Snapshot->Curves.emplace_back().Owner = value.Owner;
            a_Own = l_Snapshot->Curves.end() - 1;
        }

        a_Own->Curves[value.Slot] = curve;

        return true;
    }

    // Made whole before it joins the list, since joining may move the one it copies
    AudioMixerSnapshot& AudioMixerData::AddSnapshot(std::string_view name, UUID copyOf)
    {
        const AudioMixerSnapshot* l_Found = FindSnapshot(copyOf);
        const AudioMixerSnapshot& l_From = l_Found != nullptr ? *l_Found : GetStartSnapshot();
        AudioMixerSnapshot l_Snapshot;
        l_Snapshot.ID = UUID::Generate();
        l_Snapshot.Name = TaggedString<MemoryTag::Audio>(name);
        l_Snapshot.Curves = l_From.Curves;
        ForEachOwner(*this, [&](UUID owner)
        {
            AudioSnapshotValues& l_Entry = l_Snapshot.Values.emplace_back();
            l_Entry.Owner = owner;
            for (std::uint8_t it_Slot = 0; it_Slot < GetSlotCount(owner); ++it_Slot)
            {
                l_Entry.Values[it_Slot] = *GetValue(l_From, { owner, it_Slot });
            }
        });

        Snapshots.push_back(std::move(l_Snapshot));

        return Snapshots.back();
    }

    namespace
    {
        template<typename T>
        using AudioList = std::vector<T, TaggedAllocator<T, MemoryTag::Audio>>;

        // Every Send names a Receive or a Duck Volume, or nothing the mixer has, and no group's output comes back to it, through its parents or what is sent: a group plays into its parent, and into each group its Sends reach
        std::string ValidateSends(const AudioMixerData& data)
        {
            const std::size_t l_Count = data.Groups.size();
            const auto a_IndexOf = [&data](UUID id) { return static_cast<std::size_t>(std::ranges::find(data.Groups, id, &AudioMixerGroup::ID) - data.Groups.begin()); };
            AudioList<AudioList<std::size_t>> l_Edges(l_Count);
            for (std::size_t it_Group = 0; it_Group < l_Count; ++it_Group)
            {
                const AudioMixerGroup& l_Group = data.Groups[it_Group];
                if (l_Group.Parent)
                {
                    l_Edges[it_Group].push_back(a_IndexOf(l_Group.Parent));
                }

                for (const AudioEffect& it_Effect : l_Group.Effects)
                {
                    if (it_Effect.Type != AudioEffectType::Send || !it_Effect.Target)
                    {
                        continue;
                    }

                    for (std::size_t it_Other = 0; it_Other < l_Count; ++it_Other)
                    {
                        const auto a_Target = std::ranges::find(data.Groups[it_Other].Effects, it_Effect.Target, &AudioEffect::ID);
                        if (a_Target == data.Groups[it_Other].Effects.end() || a_Target->Type == AudioEffectType::Attenuation)
                        {
                            continue;
                        }

                        if (a_Target->Type != AudioEffectType::Receive && a_Target->Type != AudioEffectType::DuckVolume)
                        {
                            return std::format("a Send in group '{}' sends to a {}, which is not a Receive or a Duck Volume", std::string_view(l_Group.Name), GetAudioEffectInfo(a_Target->Type).Name);
                        }

                        l_Edges[it_Group].push_back(it_Other);
                    }
                }
            }

            // A walk from each group not yet walked, with the path it is on and how far along each step's edges it has got, which a loop comes back into
            AudioList<std::uint8_t> l_Walked(l_Count, 0);
            AudioList<std::pair<std::size_t, std::size_t>> l_Path;
            for (std::size_t it_Start = 0; it_Start < l_Count; ++it_Start)
            {
                if (l_Walked[it_Start] != 0)
                {
                    continue;
                }

                l_Walked[it_Start] = 1;
                l_Path.emplace_back(it_Start, 0);
                while (!l_Path.empty())
                {
                    auto& [a_Group, a_Edge] = l_Path.back();
                    if (a_Edge == l_Edges[a_Group].size())
                    {
                        l_Walked[a_Group] = 2;
                        l_Path.pop_back();

                        continue;
                    }

                    const std::size_t l_Next = l_Edges[a_Group][a_Edge++];
                    if (l_Walked[l_Next] == 1)
                    {
                        std::string l_Loop;
                        for (auto it_Step = std::ranges::find(l_Path, l_Next, &std::pair<std::size_t, std::size_t>::first); it_Step != l_Path.end(); ++it_Step)
                        {
                            l_Loop += std::format("'{}', ", std::string_view(data.Groups[it_Step->first].Name));
                        }

                        return std::format("its Sends make a loop through {}and back to '{}', which would play what they send back into it", l_Loop, std::string_view(data.Groups[l_Next].Name));
                    }

                    if (l_Walked[l_Next] == 0)
                    {
                        l_Walked[l_Next] = 1;
                        l_Path.emplace_back(l_Next, 0);
                    }
                }
            }

            return {};
        }

        // Each list's entries for groups and effects the mixer has, once each, which every check of a snapshot's values and curves needs
        template<typename Entry>
        std::string ValidateEntries(const AudioMixerData& data, const AudioMixerSnapshot& snapshot, std::span<const Entry> entries, std::string_view what)
        {
            for (auto it_Entry = entries.begin(); it_Entry != entries.end(); ++it_Entry)
            {
                if (data.GetSlotCount(it_Entry->Owner) == 0 || std::ranges::find(entries.begin(), it_Entry, it_Entry->Owner, &Entry::Owner) != it_Entry)
                {
                    return std::format("snapshot '{}' has {} for no group or effect the mixer has, or twice for one", std::string_view(snapshot.Name), what);
                }
            }

            return {};
        }

        // Every snapshot has a name and an ID no other has, StartSnapshot names one, which holds no values, every snapshot's values and curves are for groups and effects the mixer has, once each, and are numbers and curves, and every exposed value has a name no other has and is a value the mixer has that no other name exposes
        std::string ValidateSnapshots(const AudioMixerData& data)
        {
            for (auto it_Snapshot = data.Snapshots.begin(); it_Snapshot != data.Snapshots.end(); ++it_Snapshot)
            {
                const std::string_view l_Name(it_Snapshot->Name);
                if (!it_Snapshot->ID || std::ranges::find(data.Snapshots.begin(), it_Snapshot, it_Snapshot->ID, &AudioMixerSnapshot::ID) != it_Snapshot)
                {
                    return std::format("snapshot '{}' has no ID of its own", l_Name);
                }

                if (l_Name.empty() || std::ranges::find_if(data.Snapshots.begin(), it_Snapshot, [l_Name](const AudioMixerSnapshot& other) { return std::string_view(other.Name) == l_Name; }) != it_Snapshot)
                {
                    return std::format("a snapshot has no name of its own{}", l_Name.empty() ? std::string() : std::format(", '{}'", l_Name));
                }

                if (std::string l_Error = ValidateEntries(data, *it_Snapshot, std::span(it_Snapshot->Values), "values"); !l_Error.empty())
                {
                    return l_Error;
                }

                if (std::string l_Error = ValidateEntries(data, *it_Snapshot, std::span(it_Snapshot->Curves), "curves"); !l_Error.empty())
                {
                    return l_Error;
                }

                const bool l_Numbers = std::ranges::all_of(it_Snapshot->Values, [](const AudioSnapshotValues& entry) { return std::ranges::all_of(entry.Values, [](float value) { return std::isfinite(value); }); });
                const bool l_Curves = std::ranges::all_of(it_Snapshot->Curves, [](const AudioSnapshotCurves& entry) { return std::ranges::all_of(entry.Curves, [](AudioTransitionCurve curve) { return static_cast<std::size_t>(curve) < c_CurveKeys.size(); }); });
                if (!l_Numbers || !l_Curves)
                {
                    return std::format("snapshot '{}' has a value that is no number or a curve of no known kind", l_Name);
                }
            }

            const AudioMixerSnapshot* l_Start = data.FindSnapshot(data.StartSnapshot);
            if (l_Start == nullptr)
            {
                return "StartSnapshot names none of its snapshots";
            }

            if (!l_Start->Values.empty())
            {
                return std::format("its start snapshot '{}' holds values, which are its groups' and effects' own", std::string_view(l_Start->Name));
            }

            for (auto it_Exposed = data.Exposed.begin(); it_Exposed != data.Exposed.end(); ++it_Exposed)
            {
                const std::string_view l_Name(it_Exposed->Name);
                if (l_Name.empty() || std::ranges::find_if(data.Exposed.begin(), it_Exposed, [l_Name](const AudioExposedParameter& other) { return std::string_view(other.Name) == l_Name; }) != it_Exposed)
                {
                    return std::format("an exposed value has no name of its own{}", l_Name.empty() ? std::string() : std::format(", '{}'", l_Name));
                }

                if (!data.GetValueInfo(it_Exposed->Value) || std::ranges::find(data.Exposed.begin(), it_Exposed, it_Exposed->Value, &AudioExposedParameter::Value) != it_Exposed)
                {
                    return std::format("exposed value '{}' is no value the mixer has, or one another name exposes", l_Name);
                }
            }

            return {};
        }
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

    std::string_view GetAudioTransitionCurveKey(AudioTransitionCurve curve)
    {
        const std::size_t l_Index = static_cast<std::size_t>(curve);

        return c_CurveKeys[l_Index < c_CurveKeys.size() ? l_Index : 0];
    }

    std::optional<AudioTransitionCurve> FindAudioTransitionCurve(std::string_view key)
    {
        const auto a_Found = std::ranges::find(c_CurveKeys, key);

        return a_Found != c_CurveKeys.end() ? std::optional(static_cast<AudioTransitionCurve>(a_Found - c_CurveKeys.begin())) : std::nullopt;
    }

    float EvaluateAudioTransitionCurve(AudioTransitionCurve curve, float progress)
    {
        const float l_Progress = std::clamp(progress, 0.0f, 1.0f);
        switch (curve)
        {
            case AudioTransitionCurve::Smoothstep:
            {
                return l_Progress * l_Progress * (3.0f - 2.0f * l_Progress);
            }
            case AudioTransitionCurve::Squared:
            {
                return l_Progress * l_Progress;
            }
            case AudioTransitionCurve::SquareRoot:
            {
                return std::sqrt(l_Progress);
            }
            case AudioTransitionCurve::BrickwallStart:
            {
                return l_Progress > 0.0f ? 1.0f : 0.0f;
            }
            case AudioTransitionCurve::BrickwallEnd:
            {
                return l_Progress >= 1.0f ? 1.0f : 0.0f;
            }
            default:
            {
                return l_Progress;
            }
        }
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

        // Across the mixer, since a Send in one group names a Receive in another by its ID, and apart from every group's, since a snapshot names a group or an effect by its ID
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> l_EffectIDs;
        for (const AudioMixerGroup& it_Group : data.Groups)
        {
            for (const AudioEffect& it_Effect : it_Group.Effects)
            {
                if (it_Effect.Type == AudioEffectType::Attenuation)
                {
                    continue;
                }

                if (!it_Effect.ID || std::ranges::find(l_EffectIDs, it_Effect.ID) != l_EffectIDs.end() || data.FindGroup(it_Effect.ID) != nullptr)
                {
                    return std::format("a {} in group '{}' has no ID of its own", GetAudioEffectInfo(it_Effect.Type).Name, std::string_view(it_Group.Name));
                }

                l_EffectIDs.push_back(it_Effect.ID);
            }
        }

        if (std::string l_Error = ValidateSends(data); !l_Error.empty())
        {
            return l_Error;
        }

        return ValidateSnapshots(data);
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
                if (it_Effect.Type == AudioEffectType::Send && it_Effect.Target)
                {
                    l_Writer.Write("Target", it_Effect.Target);
                }

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
        l_Writer.BeginList("Snapshots");
        for (const AudioMixerSnapshot& it_Snapshot : data.Snapshots)
        {
            l_Writer.BeginItem();
            l_Writer.Write("ID", it_Snapshot.ID);
            l_Writer.Write("Name", std::string_view(it_Snapshot.Name));
            if (it_Snapshot.ID != data.StartSnapshot)
            {
                l_Writer.BeginList("Values");
                ForEachOwner(data, [&](UUID owner)
                {
                    l_Writer.BeginItem();
                    l_Writer.Write("ID", owner);
                    for (std::uint8_t it_Slot = 0; it_Slot < data.GetSlotCount(owner); ++it_Slot)
                    {
                        l_Writer.Write(data.GetValueInfo({ owner, it_Slot })->Key, *data.GetValue(it_Snapshot, { owner, it_Slot }));
                    }
                });

                l_Writer.End();
            }

            bool l_Curved = false;
            ForEachOwner(data, [&](UUID owner)
            {
                const auto a_Curves = std::ranges::find(it_Snapshot.Curves, owner, &AudioSnapshotCurves::Owner);
                const std::size_t l_Slots = data.GetSlotCount(owner);
                if (a_Curves == it_Snapshot.Curves.end() || std::all_of(a_Curves->Curves.begin(), a_Curves->Curves.begin() + static_cast<std::ptrdiff_t>(l_Slots), [](AudioTransitionCurve curve) { return curve == AudioTransitionCurve::Linear; }))
                {
                    return;
                }

                if (!l_Curved)
                {
                    l_Writer.BeginList("Curves");
                    l_Curved = true;
                }

                l_Writer.BeginItem();
                l_Writer.Write("ID", owner);
                for (std::uint8_t it_Slot = 0; it_Slot < l_Slots; ++it_Slot)
                {
                    if (a_Curves->Curves[it_Slot] != AudioTransitionCurve::Linear)
                    {
                        l_Writer.Write(data.GetValueInfo({ owner, it_Slot })->Key, GetAudioTransitionCurveKey(a_Curves->Curves[it_Slot]));
                    }
                }
            });

            if (l_Curved)
            {
                l_Writer.End();
            }
        }

        l_Writer.End();
        l_Writer.Write("StartSnapshot", data.StartSnapshot);
        if (!data.Exposed.empty())
        {
            l_Writer.BeginList("Exposed");
            for (const AudioExposedParameter& it_Exposed : data.Exposed)
            {
                l_Writer.BeginItem();
                l_Writer.Write("Name", std::string_view(it_Exposed.Name));
                l_Writer.Write("ID", it_Exposed.Value.Owner);
                l_Writer.Write("Value", data.GetValueInfo(it_Exposed.Value)->Key);
            }

            l_Writer.End();
        }

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

        // Read once the groups are, which its values are for
        const YAML::Node l_Snapshots = (*l_Root)["Snapshots"];
        const YAML::Node l_Exposed = (*l_Root)["Exposed"];
        if ((l_Snapshots && !l_Snapshots.IsSequence()) || (l_Exposed && !l_Exposed.IsSequence()))
        {
            return Unexpected{ std::string("Snapshots or Exposed is not a list") };
        }

        std::string l_Malformed;
        CookedText::ReadUUID(*l_Root, "StartSnapshot", l_Data.StartSnapshot, l_Malformed);
        if (!l_Malformed.empty())
        {
            return Unexpected{ std::format("{} is malformed", l_Malformed) };
        }

        for (std::size_t it_Snapshot = 0; l_Snapshots && it_Snapshot < l_Snapshots.size(); ++it_Snapshot)
        {
            if (std::string l_Error = ReadSnapshot(l_Snapshots[it_Snapshot], l_Data, l_Data.Snapshots.emplace_back()); !l_Error.empty())
            {
                return Unexpected{ std::move(l_Error) };
            }
        }

        if (!l_Snapshots)
        {
            l_Data.Snapshots = AudioMixerData::MakeDefault().Snapshots;
            l_Data.StartSnapshot = l_Data.Snapshots.front().ID;
        }

        for (std::size_t it_Exposed = 0; l_Exposed && it_Exposed < l_Exposed.size(); ++it_Exposed)
        {
            if (std::string l_Error = ReadExposed(l_Exposed[it_Exposed], l_Data, l_Data.Exposed.emplace_back()); !l_Error.empty())
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