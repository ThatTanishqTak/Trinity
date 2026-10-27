#include "Trinity/Audio/SoundEventPlayer.hpp"

#include "Trinity/Audio/AudioMixerData.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <tuple>
#include <utility>

namespace Trinity
{
    namespace
    {
        std::uint64_t SplitMix(std::uint64_t& state)
        {
            state += 0x9E3779B97F4A7C15ull;
            std::uint64_t l_Value = state;
            l_Value = (l_Value ^ (l_Value >> 30)) * 0xBF58476D1CE4E5B9ull;
            l_Value = (l_Value ^ (l_Value >> 27)) * 0x94D049BB133111EBull;

            return l_Value ^ (l_Value >> 31);
        }

        // FNV-1a, so a parameter set again is found without comparing every name
        std::uint64_t HashName(std::string_view name)
        {
            std::uint64_t l_Hash = 0xCBF29CE484222325ull;
            for (const char it_Character : name)
            {
                l_Hash = (l_Hash ^ static_cast<unsigned char>(it_Character)) * 0x100000001B3ull;
            }

            return l_Hash;
        }

        bool IsLater(const auto& left, const auto& right)
        {
            return std::tie(left.Time, left.Order) > std::tie(right.Time, right.Order);
        }
    }

    Ref<const SoundEventPlan> SoundEventPlan::Make(UUID event, const SoundEventAsset& asset)
    {
        Ref<SoundEventPlan> l_Plan = std::allocate_shared<SoundEventPlan>(TaggedAllocator<SoundEventPlan, MemoryTag::Audio>());
        l_Plan->Event = event;
        l_Plan->Serial = asset.GetSerial();
        l_Plan->Data = asset.GetData();
        l_Plan->Clips.assign(asset.GetClips().begin(), asset.GetClips().end());

        const auto& l_Containers = l_Plan->Data.Containers;
        l_Plan->Nodes.resize(l_Containers.size());
        for (std::size_t it_Container = 0; it_Container < l_Containers.size(); ++it_Container)
        {
            const SoundContainer& l_Container = l_Containers[it_Container];
            Node& l_Node = l_Plan->Nodes[it_Container];
            l_Node.FirstChild = static_cast<std::uint32_t>(l_Plan->Children.size());
            for (std::size_t it_Child = it_Container + 1; it_Child < l_Containers.size(); ++it_Child)
            {
                if (l_Containers[it_Child].Parent == l_Container.ID)
                {
                    l_Plan->Children.push_back(static_cast<std::uint32_t>(it_Child));
                }
            }

            l_Node.ChildCount = static_cast<std::uint32_t>(l_Plan->Children.size()) - l_Node.FirstChild;
            if (l_Container.Type == SoundContainerType::Clip)
            {
                const auto a_Clip = std::ranges::find(l_Plan->Clips, l_Container.Clip);
                l_Node.Clip = a_Clip != l_Plan->Clips.end() ? static_cast<std::uint32_t>(a_Clip - l_Plan->Clips.begin()) : c_None;
            }

            if (l_Container.Type == SoundContainerType::Switch && l_Container.Parameter)
            {
                const auto a_Parameter = std::ranges::find(l_Plan->Data.Parameters, l_Container.Parameter, &SoundParameter::ID);
                l_Node.Parameter = a_Parameter != l_Plan->Data.Parameters.end() ? static_cast<std::uint32_t>(a_Parameter - l_Plan->Data.Parameters.begin()) : c_None;
            }
        }

        // A child's case once its Switch's parameter is known, which comes before it
        for (std::size_t it_Container = 0; it_Container < l_Containers.size(); ++it_Container)
        {
            const Node& l_Parent = l_Plan->Nodes[it_Container];
            if (l_Containers[it_Container].Type != SoundContainerType::Switch || l_Parent.Parameter == c_None)
            {
                continue;
            }

            const auto& l_Labels = l_Plan->Data.Parameters[l_Parent.Parameter].Labels;
            for (std::uint32_t it_Child = 0; it_Child < l_Parent.ChildCount; ++it_Child)
            {
                const std::uint32_t l_Child = l_Plan->Children[l_Parent.FirstChild + it_Child];
                const auto a_Label = std::ranges::find(l_Labels, l_Containers[l_Child].Case);
                l_Plan->Nodes[l_Child].Case = !l_Containers[l_Child].Case.empty() && a_Label != l_Labels.end() ? static_cast<std::uint32_t>(a_Label - l_Labels.begin()) : c_None;
            }
        }

        return l_Plan;
    }

    bool SoundEventMemory::History::WasPicked(std::uint32_t child, std::uint32_t last) const
    {
        for (std::uint32_t it_Back = 0; it_Back < std::min(last, Count); ++it_Back)
        {
            if (Picks[(Next + c_MaxAvoid - 1 - it_Back) % c_MaxAvoid] == child)
            {
                return true;
            }
        }

        return false;
    }

    void SoundEventMemory::History::Add(std::uint32_t child)
    {
        Picks[Next] = child;
        Next = (Next + 1) % c_MaxAvoid;
        Count = std::min<std::uint32_t>(Count + 1, c_MaxAvoid);
    }

    SoundEventMemory::History& SoundEventMemory::FindHistory(UUID container)
    {
        const auto a_Found = std::ranges::find(Histories, container, &History::Container);
        if (a_Found != Histories.end())
        {
            return *a_Found;
        }

        History& l_History = Histories.emplace_back();
        l_History.Container = container;

        return l_History;
    }

    std::optional<float> SoundEventMemory::FindValue(std::string_view name) const
    {
        const std::uint64_t l_Hash = HashName(name);
        const auto a_Found = std::ranges::find_if(Values, [l_Hash, name](const Value& value) { return value.Hash == l_Hash && std::string_view(value.Name) == name; });

        return a_Found != Values.end() ? std::optional(a_Found->Number) : std::nullopt;
    }

    void SoundEventMemory::SetValue(std::string_view name, float number)
    {
        const std::uint64_t l_Hash = HashName(name);
        const auto a_Found = std::ranges::find_if(Values, [l_Hash, name](const Value& value) { return value.Hash == l_Hash && std::string_view(value.Name) == name; });
        if (a_Found != Values.end())
        {
            a_Found->Number = number;

            return;
        }

        Values.push_back({ l_Hash, TaggedString<MemoryTag::Audio>(name), number });
    }

    SoundEventRandom::SoundEventRandom(std::uint64_t seed)
    {
        for (std::uint64_t& it_Word : m_State)
        {
            it_Word = SplitMix(seed);
        }
    }

    std::uint64_t SoundEventRandom::MakeSeed(UUID event, UUID entity, std::uint64_t plays)
    {
        std::uint64_t l_State = event.GetValue();
        l_State = SplitMix(l_State) ^ entity.GetValue();
        l_State = SplitMix(l_State) ^ plays;

        return SplitMix(l_State);
    }

    std::uint64_t SoundEventRandom::Next()
    {
        const std::uint64_t l_Result = std::rotl(m_State[1] * 5, 7) * 9;
        const std::uint64_t l_Shifted = m_State[1] << 17;
        m_State[2] ^= m_State[0];
        m_State[3] ^= m_State[1];
        m_State[1] ^= m_State[2];
        m_State[0] ^= m_State[3];
        m_State[2] ^= l_Shifted;
        m_State[3] = std::rotl(m_State[3], 45);

        return l_Result;
    }

    float SoundEventRandom::NextFloat()
    {
        return static_cast<float>(Next() >> 40) * (1.0f / 16777216.0f);
    }

    SoundEventPlayer::SoundEventPlayer(Ref<const SoundEventPlan> plan, std::span<const SoundEventClip> clips, std::uint64_t seed, std::uint32_t sampleRate) : m_Plan(std::move(plan)), m_Clips(clips.begin(), clips.end()), m_Random(seed), m_SampleRate(sampleRate != 0 ? sampleRate : 48000)
    {
        Activate(0, c_NoActivation, 0);
    }

    // Each action may add more before the horizon, which are taken in turn
    void SoundEventPlayer::Schedule(std::uint64_t horizon, SoundEventMemory& memory, PlayList& plays)
    {
        m_Memory = &memory;
        m_Plays = &plays;
        while (!m_Actions.empty() && m_Actions.front().Time < horizon)
        {
            std::ranges::pop_heap(m_Actions, [](const Action& left, const Action& right) { return IsLater(left, right); });
            const Action l_Action = m_Actions.back();
            m_Actions.pop_back();
            switch (l_Action.Kind)
            {
                case ActionKind::Begin:
                {
                    Begin(l_Action.Activation, l_Action.Time);

                    break;
                }
                case ActionKind::ChildEnded:
                {
                    ChildEnded(l_Action.Activation, l_Action.Time);

                    break;
                }
                case ActionKind::Trigger:
                {
                    m_Activations[l_Action.Activation].TriggerPending = false;
                    Trigger(l_Action.Activation, l_Action.Time);

                    break;
                }
            }
        }

        m_Memory = nullptr;
        m_Plays = nullptr;
    }

    bool SoundEventPlayer::IsFinished() const
    {
        return m_Ended && m_Actions.empty();
    }

    // Its volume, pitch and delay drawn now, whatever its type, so the draws that follow are the same whatever it holds
    void SoundEventPlayer::Activate(std::uint32_t node, std::uint32_t parent, std::uint64_t time)
    {
        const SoundContainer& l_Container = m_Plan->Data.Containers[node];
        const float l_ParentGain = parent != c_NoActivation ? m_Activations[parent].Gain : 1.0f;
        const float l_ParentPitch = parent != c_NoActivation ? m_Activations[parent].Pitch : 1.0f;
        const float l_VolumeSpread = std::clamp(l_Container.VolumeRandom, 0.0f, SoundEventData::c_MaximumVolumeRandom);
        const float l_PitchSpread = std::clamp(l_Container.PitchRandom, 0.0f, SoundEventData::c_MaximumPitchRandom);
        const float l_Volume = std::clamp(l_Container.Volume + (2.0f * m_Random.NextFloat() - 1.0f) * l_VolumeSpread, SoundEventData::c_MinimumVolume, SoundEventData::c_MaximumVolume);
        const float l_Pitch = std::clamp(l_Container.Pitch, SoundEventData::c_MinimumPitch, SoundEventData::c_MaximumPitch) * (1.0f + (2.0f * m_Random.NextFloat() - 1.0f) * l_PitchSpread);
        const float l_Delay = std::max(l_Container.Delay, 0.0f) + m_Random.NextFloat() * std::max(l_Container.DelayRandom, 0.0f);

        std::uint32_t l_Index = 0;
        if (!m_Free.empty())
        {
            l_Index = m_Free.back();
            m_Free.pop_back();
        }
        else
        {
            l_Index = static_cast<std::uint32_t>(m_Activations.size());
            m_Activations.emplace_back();
        }

        Activation& l_Activation = m_Activations[l_Index];
        l_Activation = Activation();
        l_Activation.Node = node;
        l_Activation.Parent = parent;
        l_Activation.Gain = l_ParentGain * AudioDecibelsToGain(l_Volume);
        l_Activation.Pitch = std::clamp(l_ParentPitch * l_Pitch, SoundEventData::c_MinimumPitch, SoundEventData::c_MaximumPitch);
        Push(time + ToFrames(l_Delay), l_Index, ActionKind::Begin);
    }

    void SoundEventPlayer::Begin(std::uint32_t activation, std::uint64_t time)
    {
        const std::uint32_t l_NodeIndex = m_Activations[activation].Node;
        const SoundEventPlan::Node& l_Node = m_Plan->Nodes[l_NodeIndex];
        const SoundContainer& l_Container = m_Plan->Data.Containers[l_NodeIndex];
        m_Activations[activation].End = time;
        switch (l_Container.Type)
        {
            case SoundContainerType::Clip:
            {
                const SoundEventClip l_Clip = l_Node.Clip != SoundEventPlan::c_None ? m_Clips[l_Node.Clip] : SoundEventClip();
                if (l_Clip.Frames == 0 || l_Clip.SampleRate == 0)
                {
                    End(activation, time);

                    return;
                }

                const Activation& l_Activation = m_Activations[activation];
                const double l_Rate = static_cast<double>(l_Clip.SampleRate) / m_SampleRate * l_Activation.Pitch;
                const std::uint64_t l_Length = l_Container.Loop ? c_Never : std::max<std::uint64_t>(static_cast<std::uint64_t>(std::ceil(static_cast<double>(l_Clip.Frames) / l_Rate)), 1);
                m_Plays->push_back({ l_Node.Clip, time, l_Length, l_Rate, l_Activation.Gain, l_Container.Loop });
                // One that loops plays for good, so its parent never ends either
                if (!l_Container.Loop)
                {
                    End(activation, time + l_Length);
                }

                return;
            }
            case SoundContainerType::Layer:
            {
                m_Activations[activation].Running = l_Node.ChildCount;
                for (std::uint32_t it_Child = 0; it_Child < l_Node.ChildCount; ++it_Child)
                {
                    Activate(m_Plan->Children[l_Node.FirstChild + it_Child], activation, time);
                }

                break;
            }
            case SoundContainerType::Switch:
            {
                std::uint32_t l_Label = SoundEventPlan::c_None;
                if (l_Node.Parameter != SoundEventPlan::c_None)
                {
                    const SoundParameter& l_Parameter = m_Plan->Data.Parameters[l_Node.Parameter];
                    const float l_Value = m_Memory->FindValue(std::string_view(l_Parameter.Name)).value_or(l_Parameter.Default);
                    const float l_Last = static_cast<float>(l_Parameter.Labels.size() - 1);
                    l_Label = static_cast<std::uint32_t>(std::lround(std::isfinite(l_Value) ? std::clamp(l_Value, 0.0f, l_Last) : 0.0f));
                }

                // The children for the label, or else those for none
                const auto a_Plays = [this, &l_Node](std::uint32_t label)
                {
                    std::uint32_t l_Count = 0;
                    for (std::uint32_t it_Child = 0; it_Child < l_Node.ChildCount; ++it_Child)
                    {
                        l_Count += m_Plan->Nodes[m_Plan->Children[l_Node.FirstChild + it_Child]].Case == label ? 1u : 0u;
                    }

                    return l_Count;
                };

                const std::uint32_t l_Case = l_Label != SoundEventPlan::c_None && a_Plays(l_Label) != 0 ? l_Label : SoundEventPlan::c_None;
                m_Activations[activation].Running = a_Plays(l_Case);
                for (std::uint32_t it_Child = 0; it_Child < l_Node.ChildCount; ++it_Child)
                {
                    const std::uint32_t l_Child = m_Plan->Children[l_Node.FirstChild + it_Child];
                    if (m_Plan->Nodes[l_Child].Case == l_Case)
                    {
                        Activate(l_Child, activation, time);
                    }
                }

                break;
            }
            case SoundContainerType::Sequence:
            {
                if (l_Node.ChildCount != 0)
                {
                    m_Activations[activation].Last = time;
                    m_Activations[activation].Running = 1;
                    Activate(m_Plan->Children[l_Node.FirstChild], activation, time);
                }

                break;
            }
            case SoundContainerType::Random:
            {
                if (l_Node.ChildCount != 0)
                {
                    Trigger(activation, time);

                    return;
                }

                break;
            }
        }

        if (m_Activations[activation].Running == 0)
        {
            End(activation, time);
        }
    }

    // One pick, then the next trigger: on its interval from this one with Pulse, and with Offset on its interval from when this one ends, at once if it picked nothing
    void SoundEventPlayer::Trigger(std::uint32_t activation, std::uint64_t time)
    {
        const SoundContainer& l_Container = m_Plan->Data.Containers[m_Activations[activation].Node];
        const std::uint32_t l_Child = Pick(m_Activations[activation]);
        m_Activations[activation].Step += 1;
        m_Activations[activation].Last = time;
        if (l_Child != SoundEventPlan::c_None)
        {
            m_Activations[activation].Running += 1;
            Activate(l_Child, activation, time);
        }

        const bool l_More = l_Container.AutoTrigger && (l_Container.TriggerCount == 0 || m_Activations[activation].Step < l_Container.TriggerCount);
        if (l_More && (l_Container.Timing == SoundTriggerTiming::Pulse || l_Child == SoundEventPlan::c_None))
        {
            m_Activations[activation].TriggerPending = true;
            Push(time + DrawInterval(l_Container), activation, ActionKind::Trigger);
        }

        const Activation& l_Activation = m_Activations[activation];
        if (!l_Activation.TriggerPending && l_Activation.Running == 0)
        {
            End(activation, std::max(l_Activation.End, time));
        }
    }

    void SoundEventPlayer::ChildEnded(std::uint32_t activation, std::uint64_t time)
    {
        Activation& l_Activation = m_Activations[activation];
        const SoundEventPlan::Node& l_Node = m_Plan->Nodes[l_Activation.Node];
        const SoundContainer& l_Container = m_Plan->Data.Containers[l_Activation.Node];
        l_Activation.Running -= 1;
        l_Activation.End = std::max(l_Activation.End, time);
        switch (l_Container.Type)
        {
            case SoundContainerType::Sequence:
            {
                l_Activation.Step += 1;
                // A round that took no time at all is not looped, or it would loop for good on one frame
                const bool l_Again = l_Container.Loop && time > l_Activation.Last;
                if (l_Activation.Step >= l_Node.ChildCount && !l_Again)
                {
                    End(activation, time);

                    return;
                }

                if (l_Activation.Step >= l_Node.ChildCount)
                {
                    l_Activation.Step = 0;
                    l_Activation.Last = time;
                }

                l_Activation.Running = 1;
                Activate(m_Plan->Children[l_Node.FirstChild + l_Activation.Step], activation, time);

                return;
            }
            case SoundContainerType::Random:
            {
                const bool l_More = l_Container.AutoTrigger && (l_Container.TriggerCount == 0 || l_Activation.Step < l_Container.TriggerCount);
                if (l_More && l_Container.Timing == SoundTriggerTiming::Offset)
                {
                    l_Activation.TriggerPending = true;
                    Push(time + DrawInterval(l_Container), activation, ActionKind::Trigger);

                    return;
                }

                if (l_Activation.Running == 0 && !l_Activation.TriggerPending)
                {
                    End(activation, l_Activation.End);
                }

                return;
            }
            case SoundContainerType::Clip:
            case SoundContainerType::Layer:
            case SoundContainerType::Switch:
            {
                if (l_Activation.Running == 0)
                {
                    End(activation, l_Activation.End);
                }

                return;
            }
        }
    }

    // Its slot is free for the next container started, and its parent hears of it when its time comes
    void SoundEventPlayer::End(std::uint32_t activation, std::uint64_t time)
    {
        const std::uint32_t l_Parent = m_Activations[activation].Parent;
        m_Free.push_back(activation);
        if (l_Parent == c_NoActivation)
        {
            m_Ended = true;
            m_End = time;

            return;
        }

        Push(time, l_Parent, ActionKind::ChildEnded);
    }

    void SoundEventPlayer::Push(std::uint64_t time, std::uint32_t activation, ActionKind kind)
    {
        m_Actions.push_back({ time, m_NextOrder++, activation, kind });
        std::ranges::push_heap(m_Actions, [](const Action& left, const Action& right) { return IsLater(left, right); });
    }

    // By the children's weights, leaving out those among the Random's last picks, up to one fewer than it has. None when every child left has no weight. One number is drawn whatever is picked
    std::uint32_t SoundEventPlayer::Pick(const Activation& activation)
    {
        const SoundEventPlan::Node& l_Node = m_Plan->Nodes[activation.Node];
        const SoundContainer& l_Container = m_Plan->Data.Containers[activation.Node];
        SoundEventMemory::History& l_History = m_Memory->FindHistory(l_Container.ID);
        const std::uint32_t l_Avoid = std::min({ l_Container.AvoidRepeat, l_Node.ChildCount - 1, static_cast<std::uint32_t>(SoundEventMemory::c_MaxAvoid) });
        const auto a_Weight = [this, &l_Node](std::uint32_t child) { return static_cast<double>(std::max(m_Plan->Data.Containers[m_Plan->Children[l_Node.FirstChild + child]].Weight, 0.0f)); };

        double l_Total = 0.0;
        for (std::uint32_t it_Child = 0; it_Child < l_Node.ChildCount; ++it_Child)
        {
            l_Total += l_History.WasPicked(it_Child, l_Avoid) ? 0.0 : a_Weight(it_Child);
        }

        const double l_Target = static_cast<double>(m_Random.NextFloat()) * l_Total;
        if (l_Total <= 0.0)
        {
            return SoundEventPlan::c_None;
        }

        std::uint32_t l_Chosen = SoundEventPlan::c_None;
        double l_Sum = 0.0;
        for (std::uint32_t it_Child = 0; it_Child < l_Node.ChildCount; ++it_Child)
        {
            if (l_History.WasPicked(it_Child, l_Avoid) || a_Weight(it_Child) <= 0.0)
            {
                continue;
            }

            l_Chosen = it_Child;
            l_Sum += a_Weight(it_Child);
            if (l_Target < l_Sum)
            {
                break;
            }
        }

        l_History.Add(l_Chosen);

        return m_Plan->Children[l_Node.FirstChild + l_Chosen];
    }

    std::uint64_t SoundEventPlayer::DrawInterval(const SoundContainer& container)
    {
        const float l_Low = std::min(container.IntervalMinimum, container.IntervalMaximum);
        const float l_High = std::max(container.IntervalMinimum, container.IntervalMaximum);

        return std::max<std::uint64_t>(ToFrames(std::max(l_Low + m_Random.NextFloat() * (l_High - l_Low), c_MinimumInterval)), 1);
    }

    std::uint64_t SoundEventPlayer::ToFrames(float seconds) const
    {
        return static_cast<std::uint64_t>(std::llround(static_cast<double>(std::max(seconds, 0.0f)) * m_SampleRate));
    }

    SoundEventMix::SoundEventMix(std::uint32_t channels, std::uint32_t sampleRate, std::uint64_t clock) : m_Channels(std::max(channels, 1u)), m_SampleRate(sampleRate), m_Clock(clock), m_Mixed(clock)
    {
        static const ma_data_source_vtable s_VTable{ &OnRead, &OnSeek, &OnGetDataFormat, &OnGetCursor, nullptr, &OnSetLooping, 0 };

        ma_data_source_config l_Config = ma_data_source_config_init();
        l_Config.vtable = &s_VTable;
        static_cast<void>(ma_data_source_init(&l_Config, &m_Source.Base));
        m_Source.Mix = this;
    }

    SoundEventMix::~SoundEventMix()
    {
        ma_data_source_uninit(&m_Source.Base);
    }

    bool SoundEventMix::Offer(SoundEventMixPlay& play)
    {
        const std::uint32_t l_Tail = m_QueueTail.load(std::memory_order_relaxed);
        if (l_Tail - m_QueueHead.load(std::memory_order_acquire) >= c_MaxPlays)
        {
            return false;
        }

        m_Queue[l_Tail % c_MaxPlays] = &play;
        m_QueueTail.store(l_Tail + 1, std::memory_order_release);

        return true;
    }

    void SoundEventMix::Finish()
    {
        m_Finished.store(true, std::memory_order_release);
    }

    // On the audio thread. Whether the last play has been offered is read before the queue, so every play offered before it is seen with it
    ma_result SoundEventMix::OnRead(ma_data_source* source, void* frames, ma_uint64 frameCount, ma_uint64* framesRead)
    {
        SoundEventMix& l_Mix = *reinterpret_cast<Source*>(source)->Mix;
        const bool l_Finished = l_Mix.m_Finished.load(std::memory_order_acquire);
        std::uint32_t l_Head = l_Mix.m_QueueHead.load(std::memory_order_relaxed);
        const std::uint32_t l_Tail = l_Mix.m_QueueTail.load(std::memory_order_acquire);
        for (; l_Head != l_Tail; ++l_Head)
        {
            SoundEventMixPlay* l_Play = l_Mix.m_Queue[l_Head % c_MaxPlays];
            if (l_Mix.m_ActiveCount < c_MaxPlays)
            {
                l_Mix.m_Active[l_Mix.m_ActiveCount++] = l_Play;
            }
            else
            {
                l_Play->Done.store(true, std::memory_order_release);
            }
        }

        l_Mix.m_QueueHead.store(l_Head, std::memory_order_release);
        if (l_Finished && l_Mix.m_ActiveCount == 0)
        {
            *framesRead = 0;

            return MA_AT_END;
        }

        float* l_Output = static_cast<float*>(frames);
        if (l_Output != nullptr)
        {
            std::fill_n(l_Output, static_cast<std::size_t>(frameCount) * l_Mix.m_Channels, 0.0f);
        }

        for (std::uint32_t it_Play = 0; it_Play < l_Mix.m_ActiveCount;)
        {
            SoundEventMixPlay* l_Play = l_Mix.m_Active[it_Play];
            if (!l_Mix.Render(*l_Play, l_Output, frameCount, l_Mix.m_Mixed))
            {
                ++it_Play;

                continue;
            }

            // Let go of last, as the main thread may take it back at once
            l_Mix.m_Active[it_Play] = l_Mix.m_Active[--l_Mix.m_ActiveCount];
            l_Play->Done.store(true, std::memory_order_release);
        }

        l_Mix.m_Mixed += frameCount;
        l_Mix.m_Clock.store(l_Mix.m_Mixed, std::memory_order_release);
        *framesRead = frameCount;

        return MA_SUCCESS;
    }

    ma_result SoundEventMix::OnSeek([[maybe_unused]] ma_data_source* source, [[maybe_unused]] ma_uint64 frame)
    {
        return MA_NOT_IMPLEMENTED;
    }

    ma_result SoundEventMix::OnGetDataFormat(ma_data_source* source, ma_format* format, ma_uint32* channels, ma_uint32* sampleRate, ma_channel* channelMap, size_t channelMapCapacity)
    {
        const SoundEventMix& l_Mix = *reinterpret_cast<Source*>(source)->Mix;
        *format = ma_format_f32;
        *channels = l_Mix.m_Channels;
        *sampleRate = l_Mix.m_SampleRate;
        ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCapacity, l_Mix.m_Channels);

        return MA_SUCCESS;
    }

    ma_result SoundEventMix::OnGetCursor(ma_data_source* source, ma_uint64* cursor)
    {
        *cursor = reinterpret_cast<Source*>(source)->Mix->m_Clock.load(std::memory_order_acquire);

        return MA_SUCCESS;
    }

    // Its plays loop or not by themselves
    ma_result SoundEventMix::OnSetLooping([[maybe_unused]] ma_data_source* source, [[maybe_unused]] ma_bool32 looping)
    {
        return MA_SUCCESS;
    }

    // From the frame of the block its start falls on, or from where it would be by now when its start has passed, each frame read between the clip's two nearest samples, the one past its end being silence, and laid onto the mix's channels: a mono clip into each, into a mono mix the clip's channels averaged, and otherwise channel for channel
    bool SoundEventMix::Render(SoundEventMixPlay& play, float* output, std::uint64_t frames, std::uint64_t clock)
    {
        const std::uint32_t l_Channels = play.Channels;
        float* l_Buffer = play.Buffer.data();
        ma_data_source* l_Source = play.Voice.GetDataSource();
        if (!play.Begun)
        {
            if (play.Start >= clock + frames)
            {
                return false;
            }

            play.Begun = true;
            if (play.Start < clock)
            {
                play.Played = clock - play.Start;
                const std::uint64_t l_Frame = static_cast<std::uint64_t>(static_cast<double>(play.Played) * play.Rate);
                if ((play.Length != SoundEventPlayer::c_Never && play.Played >= play.Length) || play.Frames == 0 || (!play.Loop && l_Frame >= play.Frames))
                {
                    return true;
                }

                static_cast<void>(ma_data_source_seek_to_pcm_frame(l_Source, play.Loop ? l_Frame % play.Frames : l_Frame));
                play.BufferStart = l_Frame;
                play.Buffered = 0;
            }
        }

        // The clip's frames up to the one given held in the buffer, as far as the clip goes, the buffer moved up to the one before it first
        const auto a_Fill = [&play, l_Buffer, l_Source, l_Channels](std::uint64_t last)
        {
            if (last < play.BufferStart + play.Buffered || play.InputEnded)
            {
                return;
            }

            const std::uint64_t l_First = last - 1;
            const std::uint64_t l_End = play.BufferStart + play.Buffered;
            if (l_First < l_End)
            {
                const std::uint64_t l_Keep = l_End - l_First;
                std::memmove(l_Buffer, l_Buffer + (l_First - play.BufferStart) * l_Channels, static_cast<std::size_t>(l_Keep * l_Channels) * sizeof(float));
                play.Buffered = l_Keep;
            }
            else
            {
                // A play faster than its buffer passes over what it does not need
                for (std::uint64_t l_Skip = l_First - l_End; l_Skip > 0 && !play.InputEnded;)
                {
                    ma_uint64 l_Read = 0;
                    const std::uint64_t l_Chunk = std::min<std::uint64_t>(l_Skip, c_BufferFrames);
                    static_cast<void>(ma_data_source_read_pcm_frames(l_Source, l_Buffer, l_Chunk, &l_Read));
                    play.InputEnded = l_Read < l_Chunk;
                    l_Skip -= l_Read;
                }

                play.Buffered = 0;
            }

            play.BufferStart = l_First;
            if (play.InputEnded)
            {
                return;
            }

            ma_uint64 l_Read = 0;
            const std::uint64_t l_Space = c_BufferFrames - play.Buffered;
            static_cast<void>(ma_data_source_read_pcm_frames(l_Source, l_Buffer + play.Buffered * l_Channels, l_Space, &l_Read));
            play.Buffered += l_Read;
            play.InputEnded = l_Read < l_Space;
        };

        const std::uint64_t l_First = play.Start > clock ? play.Start - clock : 0;
        std::uint64_t l_Count = frames - l_First;
        if (play.Length != SoundEventPlayer::c_Never)
        {
            l_Count = std::min(l_Count, play.Length - play.Played);
        }

        const std::uint32_t l_MixChannels = m_Channels;
        for (std::uint64_t it_Frame = 0; it_Frame < l_Count; ++it_Frame)
        {
            const double l_Position = static_cast<double>(play.Played) * play.Rate;
            const std::uint64_t l_Index = static_cast<std::uint64_t>(l_Position);
            const float l_Fraction = static_cast<float>(l_Position - static_cast<double>(l_Index));
            a_Fill(l_Index + 1);
            const std::uint64_t l_End = play.BufferStart + play.Buffered;
            if (l_Index < play.BufferStart || l_Index >= l_End)
            {
                return true;
            }

            play.Played += 1;
            if (output == nullptr)
            {
                continue;
            }

            const float* l_Now = l_Buffer + (l_Index - play.BufferStart) * l_Channels;
            const float* l_After = l_Index + 1 < l_End ? l_Now + l_Channels : nullptr;
            const auto a_Sample = [l_Now, l_After, l_Fraction](std::uint32_t channel) { return l_Now[channel] + l_Fraction * ((l_After != nullptr ? l_After[channel] : 0.0f) - l_Now[channel]); };
            float* l_Out = output + (l_First + it_Frame) * l_MixChannels;
            if (l_Channels == l_MixChannels)
            {
                for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                {
                    l_Out[it_Channel] += play.Gain * a_Sample(it_Channel);
                }
            }
            else if (l_Channels == 1)
            {
                const float l_Sample = play.Gain * a_Sample(0);
                for (std::uint32_t it_Channel = 0; it_Channel < l_MixChannels; ++it_Channel)
                {
                    l_Out[it_Channel] += l_Sample;
                }
            }
            else if (l_MixChannels == 1)
            {
                float l_Sum = 0.0f;
                for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                {
                    l_Sum += a_Sample(it_Channel);
                }

                l_Out[0] += play.Gain * l_Sum / static_cast<float>(l_Channels);
            }
            else
            {
                for (std::uint32_t it_Channel = 0; it_Channel < std::min(l_Channels, l_MixChannels); ++it_Channel)
                {
                    l_Out[it_Channel] += play.Gain * a_Sample(it_Channel);
                }
            }
        }

        return play.Length != SoundEventPlayer::c_Never && play.Played >= play.Length;
    }
}