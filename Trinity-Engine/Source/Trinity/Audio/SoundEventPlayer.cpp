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

        // A low-pass curve's filter is worked out again this many frames apart as its cutoff moves
        constexpr std::uint32_t c_FilterStep = 32;
        // Butterworth's, so the filter is flat below its cutoff, 3 dB down at it and 12 dB an octave down above it, as the occlusion filter is
        constexpr float c_ButterworthQ = 0.70710678f;
        constexpr float c_MaximumCutoffRatio = 0.45f;
        constexpr float c_Pi = 3.14159265358979f;

        // The cookbook's low-pass, as Robert Bristow-Johnson gives it, at a cutoff kept where the filter stays stable
        void SetLowPass(SoundEventMixPlay& play, float cutoff, float sampleRate)
        {
            play.Cutoff = cutoff;
            const float l_Cutoff = std::clamp(cutoff, SoundEventData::c_MinimumCutoff, sampleRate * c_MaximumCutoffRatio);
            const float l_Omega = 2.0f * c_Pi * l_Cutoff / sampleRate;
            const float l_Cos = std::cos(l_Omega);
            const float l_Alpha = std::sin(l_Omega) / (2.0f * c_ButterworthQ);
            const float l_A0 = 1.0f + l_Alpha;
            play.B0 = (1.0f - l_Cos) * 0.5f / l_A0;
            play.B1 = (1.0f - l_Cos) / l_A0;
            play.B2 = play.B0;
            play.A1 = -2.0f * l_Cos / l_A0;
            play.A2 = (1.0f - l_Alpha) / l_A0;
        }

        float FilterSample(SoundEventMixPlay& play, std::size_t channel, float sample)
        {
            std::array<float, 2>& l_Registers = play.Registers[channel];
            const float l_Low = play.B0 * sample + l_Registers[0];
            l_Registers[0] = play.B1 * sample - play.A1 * l_Low + l_Registers[1];
            l_Registers[1] = play.B2 * sample - play.A2 * l_Low;

            return l_Low;
        }

        // A parameter moved towards a goal by as far as its speed takes it in the frames given
        float MoveTowards(float value, float goal, float speed, std::uint64_t frames, double sampleRate)
        {
            const float l_Step = static_cast<float>(static_cast<double>(speed) * static_cast<double>(frames) / sampleRate);

            return value < goal ? std::min(value + l_Step, goal) : std::max(value - l_Step, goal);
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
                    l_Plan->Nodes[it_Child].Parent = static_cast<std::uint32_t>(it_Container);
                }
            }

            // A curve with no points moves nothing. What moves a container moves everything below it, whose parent comes before it
            l_Node.FirstCurve = static_cast<std::uint32_t>(l_Plan->Curves.size());
            for (std::size_t it_Curve = 0; it_Curve < l_Container.Curves.size(); ++it_Curve)
            {
                const SoundCurve& l_Curve = l_Container.Curves[it_Curve];
                const auto a_Parameter = std::ranges::find(l_Plan->Data.Parameters, l_Curve.Parameter, &SoundParameter::ID);
                if (l_Curve.Points.empty() || a_Parameter == l_Plan->Data.Parameters.end())
                {
                    continue;
                }

                l_Plan->Curves.push_back({ static_cast<std::uint32_t>(a_Parameter - l_Plan->Data.Parameters.begin()), static_cast<std::uint32_t>(it_Container), static_cast<std::uint32_t>(it_Curve) });
                l_Node.Faded = l_Node.Faded || l_Curve.Target == SoundCurveTarget::Volume;
                l_Node.Pitched = l_Node.Pitched || l_Curve.Target == SoundCurveTarget::Pitch;
                l_Node.Filtered = l_Node.Filtered || l_Curve.Target == SoundCurveTarget::Cutoff;
            }

            l_Node.CurveCount = static_cast<std::uint32_t>(l_Plan->Curves.size()) - l_Node.FirstCurve;
            l_Plan->HasPitchCurves = l_Plan->HasPitchCurves || l_Node.Pitched;
            if (l_Node.Parent != c_None)
            {
                const Node& l_Parent = l_Plan->Nodes[l_Node.Parent];
                l_Node.Faded = l_Node.Faded || l_Parent.Faded;
                l_Node.Pitched = l_Node.Pitched || l_Parent.Pitched;
                l_Node.Filtered = l_Node.Filtered || l_Parent.Filtered;
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

    // Down the tree, each container from its parent's, which comes before it: volumes add in decibels, pitches multiply and the lowest cutoff wins
    void SoundEventPlan::Evaluate(std::span<const float> values, std::span<NodeValue> nodes) const
    {
        for (std::size_t it_Node = 0; it_Node < Nodes.size() && it_Node < nodes.size(); ++it_Node)
        {
            const Node& l_Node = Nodes[it_Node];
            NodeValue l_Value = l_Node.Parent != c_None ? nodes[l_Node.Parent] : NodeValue();
            float l_Decibels = 0.0f;
            for (std::uint32_t it_Curve = l_Node.FirstCurve; it_Curve < l_Node.FirstCurve + l_Node.CurveCount; ++it_Curve)
            {
                const Curve& l_Curve = Curves[it_Curve];
                const SoundCurve& l_Source = Data.Containers[l_Curve.Container].Curves[l_Curve.Index];
                const float l_Y = l_Source.Evaluate(l_Curve.Parameter < values.size() ? values[l_Curve.Parameter] : 0.0f);
                switch (l_Source.Target)
                {
                    case SoundCurveTarget::Volume:
                    {
                        l_Decibels += std::clamp(l_Y, SoundEventData::c_MinimumVolume, SoundEventData::c_MaximumVolume);

                        break;
                    }
                    case SoundCurveTarget::Pitch:
                    {
                        l_Value.Pitch *= std::clamp(l_Y, SoundEventData::c_MinimumPitch, SoundEventData::c_MaximumPitch);

                        break;
                    }
                    case SoundCurveTarget::Cutoff:
                    {
                        l_Value.Cutoff = std::min(l_Value.Cutoff, std::clamp(l_Y, SoundEventData::c_MinimumCutoff, SoundEventData::c_MaximumCutoff));

                        break;
                    }
                }
            }

            l_Value.Gain *= l_Decibels != 0.0f ? AudioDecibelsToGain(l_Decibels) : 1.0f;
            l_Value.Pitch = std::clamp(l_Value.Pitch, SoundEventData::c_MinimumPitch, SoundEventData::c_MaximumPitch);
            nodes[it_Node] = l_Value;
        }
    }

    std::optional<float> SoundParameterTarget::Reading::At(std::uint64_t frame) const
    {
        const bool l_Given = frame >= From ? Given : GivenBefore;

        return l_Given ? std::optional(frame >= From ? Value : Before) : std::nullopt;
    }

    void SoundParameterTarget::Set(float value, std::uint64_t frame)
    {
        Write(value, true, frame);
    }

    void SoundParameterTarget::Clear(std::uint64_t frame)
    {
        Write(m_Written.Value, false, frame);
    }

    // What it had just before the frame is kept as what it had before, so a change made before the last has started still starts from where that one leaves it
    void SoundParameterTarget::Write(float value, bool given, std::uint64_t frame)
    {
        Reading& l_Reading = m_Written;
        const std::uint64_t l_Frame = std::max(frame, l_Reading.From);
        if (l_Frame > l_Reading.From)
        {
            l_Reading.Before = l_Reading.Value;
            l_Reading.GivenBefore = l_Reading.Given;
        }

        l_Reading.Value = value;
        l_Reading.Given = given;
        l_Reading.From = l_Frame;

        const std::uint32_t l_Sequence = m_Sequence.load(std::memory_order_relaxed);
        m_Sequence.store(l_Sequence + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        m_Value.store(l_Reading.Value, std::memory_order_relaxed);
        m_Before.store(l_Reading.Before, std::memory_order_relaxed);
        m_From.store(l_Reading.From, std::memory_order_relaxed);
        m_Given.store(l_Reading.Given, std::memory_order_relaxed);
        m_GivenBefore.store(l_Reading.GivenBefore, std::memory_order_relaxed);
        m_Sequence.store(l_Sequence + 2, std::memory_order_release);
    }

    bool SoundParameterTarget::Read(Reading& reading) const
    {
        const std::uint32_t l_Sequence = m_Sequence.load(std::memory_order_acquire);
        if ((l_Sequence & 1) != 0)
        {
            return false;
        }

        Reading l_Reading;
        l_Reading.Value = m_Value.load(std::memory_order_relaxed);
        l_Reading.Before = m_Before.load(std::memory_order_relaxed);
        l_Reading.From = m_From.load(std::memory_order_relaxed);
        l_Reading.Given = m_Given.load(std::memory_order_relaxed);
        l_Reading.GivenBefore = m_GivenBefore.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (m_Sequence.load(std::memory_order_relaxed) != l_Sequence)
        {
            return false;
        }

        reading = l_Reading;

        return true;
    }

    // In two steps when the target changes between the two frames, towards what it was until then and what it is after
    float SeekSoundParameter(const SoundParameter& parameter, float value, const SoundParameterTarget::Reading& target, std::uint64_t from, std::uint64_t to, double sampleRate)
    {
        const auto a_Goal = [&parameter, &target](std::uint64_t frame) { return parameter.Clamp(target.At(frame).value_or(parameter.Default)); };
        if (parameter.Kind == SoundParameterKind::Discrete || !(parameter.SeekSpeed > 0.0f))
        {
            return a_Goal(to);
        }

        float l_Value = parameter.Clamp(value);
        if (to <= from)
        {
            return l_Value;
        }

        if (target.From > from && target.From <= to)
        {
            l_Value = MoveTowards(l_Value, a_Goal(from), parameter.SeekSpeed, target.From - from, sampleRate);
            from = target.From;
        }

        return MoveTowards(l_Value, a_Goal(to), parameter.SeekSpeed, to - from, sampleRate);
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

        return a_Found != Values.end() && a_Found->Given ? std::optional(a_Found->Number) : std::nullopt;
    }

    void SoundEventMemory::SetValue(std::string_view name, float number)
    {
        const std::uint64_t l_Hash = HashName(name);
        const auto a_Found = std::ranges::find_if(Values, [l_Hash, name](const Value& value) { return value.Hash == l_Hash && std::string_view(value.Name) == name; });
        if (a_Found != Values.end())
        {
            a_Found->Number = number;
            a_Found->Given = true;

            return;
        }

        Values.push_back({ l_Hash, TaggedString<MemoryTag::Audio>(name), number, true });
    }

    void SoundEventMemory::Reserve(std::string_view name)
    {
        const std::uint64_t l_Hash = HashName(name);
        if (std::ranges::none_of(Values, [l_Hash, name](const Value& value) { return value.Hash == l_Hash && std::string_view(value.Name) == name; }))
        {
            Values.push_back({ l_Hash, TaggedString<MemoryTag::Audio>(name), 0.0f, false });
        }
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

    SoundEventPlayer::SoundEventPlayer(Ref<const SoundEventPlan> plan, std::span<const SoundEventClip> clips, std::uint64_t seed, std::uint32_t sampleRate) : m_Plan(std::move(plan)), m_Clips(clips.begin(), clips.end()), m_Random(seed), m_SampleRate(sampleRate != 0 ? sampleRate : 48000), m_Anchored(m_Plan->HasPitchCurves)
    {
        if (m_Anchored)
        {
            m_Pitches.assign(m_Plan->Nodes.size(), 1.0f);
        }

        Activate(0, c_NoActivation, 0, c_NoPlay, 0);
    }

    // Each action may add more before the horizon, which are taken in turn
    void SoundEventPlayer::Schedule(std::uint64_t horizon, SoundEventMemory& memory, PlayList& plays, std::span<const float> values)
    {
        m_Memory = &memory;
        m_Plays = &plays;
        m_Values = values;
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
                    // A clip's end is no longer foreseen once its parent has heard of it
                    std::erase_if(m_Running, [&l_Action](const Pitched& running) { return running.Order == l_Action.Order; });
                    ChildEnded(l_Action);

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
        m_Values = {};
    }

    // Each clip as far as it has got at the pitch it had since it was last told, then foreseen to end at the pitch it has now. A clip that is the root ends the instance, which is then foreseen likewise
    void SoundEventPlayer::Retime(std::span<const float> pitches, std::uint64_t clock, PlayList& plays)
    {
        if (!m_Anchored)
        {
            return;
        }

        std::copy_n(pitches.begin(), std::min(pitches.size(), m_Pitches.size()), m_Pitches.begin());
        bool l_Moved = false;
        for (auto it_Running = m_Running.begin(); it_Running != m_Running.end();)
        {
            Pitched& l_Running = *it_Running;
            if (clock > l_Running.Clock)
            {
                l_Running.Position += static_cast<double>(clock - l_Running.Clock) * GetLiveRate(l_Running.ClipRate, l_Running.Pitch, l_Running.Live);
                l_Running.Clock = clock;
            }

            l_Running.Live = m_Pitches[l_Running.Node];
            const double l_Left = std::max(l_Running.Frames - l_Running.Position, 0.0);
            const std::uint64_t l_End = std::max(l_Running.Clock + static_cast<std::uint64_t>(std::ceil(l_Left / GetLiveRate(l_Running.ClipRate, l_Running.Pitch, l_Running.Live))), l_Running.Start + 1);
            if (l_End != l_Running.End)
            {
                l_Running.End = l_End;
                if (l_Running.Parent == c_NoActivation)
                {
                    m_End = l_End;
                }

                for (Action& it_Action : m_Actions)
                {
                    if (it_Action.Order == l_Running.Order)
                    {
                        it_Action.Time = l_End;
                        it_Action.PlayTime = it_Action.Play == l_Running.Number ? l_End : it_Action.PlayTime;
                        l_Moved = true;
                    }
                }

                for (Play& it_Play : plays)
                {
                    if (it_Play.Number == l_Running.Number)
                    {
                        it_Play.Length = l_End - it_Play.Start;
                    }
                }
            }

            if (l_Running.Parent == c_NoActivation && clock >= l_Running.End)
            {
                it_Running = m_Running.erase(it_Running);

                continue;
            }

            ++it_Running;
        }

        if (l_Moved)
        {
            std::ranges::make_heap(m_Actions, [](const Action& left, const Action& right) { return IsLater(left, right); });
        }
    }

    bool SoundEventPlayer::IsFinished() const
    {
        return m_Ended && m_Actions.empty();
    }

    // Its volume, pitch and delay drawn now, whatever its type, so the draws that follow are the same whatever it holds
    void SoundEventPlayer::Activate(std::uint32_t node, std::uint32_t parent, std::uint64_t time, std::uint32_t anchor, std::uint64_t anchorTime)
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
        l_Activation.Anchor = anchor;
        l_Activation.AnchorTime = anchorTime;
        static_cast<void>(Push(time + ToFrames(l_Delay), l_Index, ActionKind::Begin));
    }

    void SoundEventPlayer::Begin(std::uint32_t activation, std::uint64_t time)
    {
        const std::uint32_t l_NodeIndex = m_Activations[activation].Node;
        const SoundEventPlan::Node& l_Node = m_Plan->Nodes[l_NodeIndex];
        const SoundContainer& l_Container = m_Plan->Data.Containers[l_NodeIndex];
        m_Activations[activation].End = time;
        m_Activations[activation].EndPlay = m_Activations[activation].Anchor;
        m_Activations[activation].EndPlayTime = m_Activations[activation].AnchorTime;
        const std::uint32_t l_Anchor = m_Activations[activation].Anchor;
        const std::uint64_t l_AnchorTime = m_Activations[activation].AnchorTime;
        switch (l_Container.Type)
        {
            case SoundContainerType::Clip:
            {
                const SoundEventClip l_Clip = l_Node.Clip != SoundEventPlan::c_None ? m_Clips[l_Node.Clip] : SoundEventClip();
                if (l_Clip.Frames == 0 || l_Clip.SampleRate == 0)
                {
                    static_cast<void>(End(activation, time));

                    return;
                }

                // A clip whose pitch curves move is foreseen to end at the pitch they give it now
                Activation& l_Activation = m_Activations[activation];
                const double l_ClipRate = static_cast<double>(l_Clip.SampleRate) / m_SampleRate;
                const double l_Rate = static_cast<double>(l_Clip.SampleRate) / m_SampleRate * l_Activation.Pitch;
                const bool l_Pitched = m_Anchored && l_Node.Pitched;
                const double l_LiveRate = l_Pitched ? GetLiveRate(l_ClipRate, l_Activation.Pitch, m_Pitches[l_NodeIndex]) : l_Rate;
                const std::uint64_t l_Length = l_Container.Loop ? c_Never : std::max<std::uint64_t>(static_cast<std::uint64_t>(std::ceil(static_cast<double>(l_Clip.Frames) / l_LiveRate)), 1);
                const std::uint32_t l_Number = m_NextPlay++;
                const bool l_Follows = m_Anchored && l_Anchor != c_NoPlay;
                m_Plays->push_back({ l_Node.Clip, time, l_Length, l_Rate, l_Activation.Gain, l_Container.Loop, l_Activation.Pitch, l_NodeIndex, l_Number, l_Follows ? l_Anchor : c_NoPlay, l_Follows ? time - std::min(time, l_AnchorTime) : 0 });
                // One that loops plays for good, so its parent never ends either
                if (l_Container.Loop)
                {
                    return;
                }

                if (m_Anchored)
                {
                    l_Activation.EndPlay = l_Number;
                    l_Activation.EndPlayTime = time + l_Length;
                }

                const std::uint32_t l_Parent = l_Activation.Parent;
                const std::uint64_t l_Order = End(activation, time + l_Length);
                if (l_Pitched)
                {
                    m_Running.push_back({ l_Number, l_NodeIndex, l_Parent, l_Order, time, time + l_Length, static_cast<double>(l_Clip.Frames), l_ClipRate, l_Activation.Pitch, 0.0, time, m_Pitches[l_NodeIndex] });
                }

                return;
            }
            case SoundContainerType::Layer:
            {
                m_Activations[activation].Running = l_Node.ChildCount;
                for (std::uint32_t it_Child = 0; it_Child < l_Node.ChildCount; ++it_Child)
                {
                    Activate(m_Plan->Children[l_Node.FirstChild + it_Child], activation, time, l_Anchor, l_AnchorTime);
                }

                break;
            }
            case SoundContainerType::Switch:
            {
                std::uint32_t l_Label = SoundEventPlan::c_None;
                if (l_Node.Parameter != SoundEventPlan::c_None)
                {
                    const SoundParameter& l_Parameter = m_Plan->Data.Parameters[l_Node.Parameter];
                    const float l_Value = l_Node.Parameter < m_Values.size() ? m_Values[l_Node.Parameter] : m_Memory->FindValue(std::string_view(l_Parameter.Name)).value_or(l_Parameter.Default);
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
                        Activate(l_Child, activation, time, l_Anchor, l_AnchorTime);
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
                    Activate(m_Plan->Children[l_Node.FirstChild], activation, time, l_Anchor, l_AnchorTime);
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
            static_cast<void>(End(activation, time));
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
            Activate(l_Child, activation, time, m_Activations[activation].Anchor, m_Activations[activation].AnchorTime);
        }

        const bool l_More = l_Container.AutoTrigger && (l_Container.TriggerCount == 0 || m_Activations[activation].Step < l_Container.TriggerCount);
        if (l_More && (l_Container.Timing == SoundTriggerTiming::Pulse || l_Child == SoundEventPlan::c_None))
        {
            m_Activations[activation].TriggerPending = true;
            static_cast<void>(Push(time + DrawInterval(l_Container), activation, ActionKind::Trigger));
        }

        const Activation& l_Activation = m_Activations[activation];
        if (!l_Activation.TriggerPending && l_Activation.Running == 0)
        {
            static_cast<void>(End(activation, std::max(l_Activation.End, time)));
        }
    }

    // The child that ends last is the end of its parent, and what starts once it has follows the play that is its end
    void SoundEventPlayer::ChildEnded(const Action& action)
    {
        const std::uint32_t activation = action.Activation;
        const std::uint64_t time = action.Time;
        Activation& l_Activation = m_Activations[activation];
        const SoundEventPlan::Node& l_Node = m_Plan->Nodes[l_Activation.Node];
        const SoundContainer& l_Container = m_Plan->Data.Containers[l_Activation.Node];
        l_Activation.Running -= 1;
        if (time >= l_Activation.End)
        {
            l_Activation.EndPlay = action.Play;
            l_Activation.EndPlayTime = action.PlayTime;
        }

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
                    static_cast<void>(End(activation, time));

                    return;
                }

                if (l_Activation.Step >= l_Node.ChildCount)
                {
                    l_Activation.Step = 0;
                    l_Activation.Last = time;
                }

                l_Activation.Running = 1;
                Activate(m_Plan->Children[l_Node.FirstChild + l_Activation.Step], activation, time, action.Play, action.PlayTime);

                return;
            }
            case SoundContainerType::Random:
            {
                const bool l_More = l_Container.AutoTrigger && (l_Container.TriggerCount == 0 || l_Activation.Step < l_Container.TriggerCount);
                if (l_More && l_Container.Timing == SoundTriggerTiming::Offset)
                {
                    l_Activation.TriggerPending = true;
                    l_Activation.Anchor = action.Play;
                    l_Activation.AnchorTime = action.PlayTime;
                    static_cast<void>(Push(time + DrawInterval(l_Container), activation, ActionKind::Trigger));

                    return;
                }

                if (l_Activation.Running == 0 && !l_Activation.TriggerPending)
                {
                    static_cast<void>(End(activation, l_Activation.End));
                }

                return;
            }
            case SoundContainerType::Clip:
            case SoundContainerType::Layer:
            case SoundContainerType::Switch:
            {
                if (l_Activation.Running == 0)
                {
                    static_cast<void>(End(activation, l_Activation.End));
                }

                return;
            }
        }
    }

    // Its slot is free for the next container started, and its parent hears of it when its time comes, with the play that is its end
    std::uint64_t SoundEventPlayer::End(std::uint32_t activation, std::uint64_t time)
    {
        const Activation& l_Activation = m_Activations[activation];
        const std::uint32_t l_Parent = l_Activation.Parent;
        const std::uint32_t l_Play = l_Activation.EndPlay;
        const std::uint64_t l_PlayTime = l_Activation.EndPlayTime;
        m_Free.push_back(activation);
        if (l_Parent == c_NoActivation)
        {
            m_Ended = true;
            m_End = time;

            return c_Never;
        }

        return Push(time, l_Parent, ActionKind::ChildEnded, l_Play, l_PlayTime);
    }

    std::uint64_t SoundEventPlayer::Push(std::uint64_t time, std::uint32_t activation, ActionKind kind, std::uint32_t play, std::uint64_t playTime)
    {
        const std::uint64_t l_Order = m_NextOrder++;
        m_Actions.push_back({ time, l_Order, activation, kind, play, playTime });
        std::ranges::push_heap(m_Actions, [](const Action& left, const Action& right) { return IsLater(left, right); });

        return l_Order;
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

    double SoundEventPlayer::GetLiveRate(double clipRate, float pitch, float live)
    {
        return clipRate * static_cast<double>(std::clamp(pitch * live, SoundEventData::c_MinimumPitch, SoundEventData::c_MaximumPitch));
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

    void SoundEventMix::Control(const SoundEventPlan& plan, std::span<const SoundParameterTarget* const> targets, std::span<const float> values, const ma_node_graph* graph, std::uint64_t frame)
    {
        const std::size_t l_Count = std::min({ targets.size(), values.size(), plan.Data.Parameters.size() });
        m_Plan = &plan;
        m_Graph = graph;
        m_Frame = frame;
        m_Targets.assign(targets.begin(), targets.begin() + static_cast<std::ptrdiff_t>(l_Count));
        m_Readings.assign(l_Count, {});
        m_Values.assign(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(l_Count));
        m_Published = decltype(m_Published)(l_Count);
        for (std::size_t it_Parameter = 0; it_Parameter < l_Count; ++it_Parameter)
        {
            m_Readings[it_Parameter] = m_Targets[it_Parameter]->GetWritten();
            m_Published[it_Parameter].store(m_Values[it_Parameter], std::memory_order_relaxed);
        }

        m_Nodes.assign(plan.Nodes.size(), {});
        plan.Evaluate(m_Values, m_Nodes);
        m_Update = 1;
    }

    float SoundEventMix::GetValue(std::size_t parameter) const
    {
        return parameter < m_Published.size() ? m_Published[parameter].load(std::memory_order_relaxed) : 0.0f;
    }

    // On the audio thread. A target the main thread is writing is read again at the next time, its reading before kept meanwhile
    void SoundEventMix::Seek()
    {
        if (m_Plan == nullptr || m_Graph == nullptr || m_Values.empty())
        {
            return;
        }

        const std::uint64_t l_Frame = ma_node_graph_get_time(m_Graph);
        if (l_Frame == m_Frame)
        {
            return;
        }

        const std::uint64_t l_From = std::min(m_Frame, l_Frame);
        for (std::size_t it_Parameter = 0; it_Parameter < m_Values.size(); ++it_Parameter)
        {
            static_cast<void>(m_Targets[it_Parameter]->Read(m_Readings[it_Parameter]));
            m_Values[it_Parameter] = SeekSoundParameter(m_Plan->Data.Parameters[it_Parameter], m_Values[it_Parameter], m_Readings[it_Parameter], l_From, l_Frame, static_cast<double>(m_SampleRate));
            m_Published[it_Parameter].store(m_Values[it_Parameter], std::memory_order_relaxed);
        }

        m_Frame = l_Frame;
        if (!m_Plan->Curves.empty())
        {
            m_Plan->Evaluate(m_Values, m_Nodes);
            m_RampFrames = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(l_Frame - l_From, 1, c_MaxRampFrames));
            ++m_Update;
        }
    }

    // A play its curves move nothing on stays as it was given
    void SoundEventMix::Follow(SoundEventMixPlay& play, bool begin) const
    {
        if (m_Plan == nullptr || play.Update == m_Update || !(play.Faded || play.Pitched || play.Filtered) || play.Node >= m_Nodes.size())
        {
            return;
        }

        const SoundEventPlan::NodeValue& l_Value = m_Nodes[play.Node];
        play.Update = m_Update;
        play.GainTo = play.Faded ? l_Value.Gain : 1.0f;
        play.PitchTo = play.Pitched ? l_Value.Pitch : 1.0f;
        play.CutoffTo = play.Filtered ? l_Value.Cutoff : SoundEventData::c_MaximumCutoff;
        if (begin)
        {
            play.LiveGain = play.GainTo;
            play.LivePitch = play.PitchTo;
            play.RampLeft = 0;
            SetLowPass(play, play.CutoffTo, static_cast<float>(m_SampleRate));

            return;
        }

        play.RampFrames = m_RampFrames;
        play.RampLeft = m_RampFrames;
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
                // Not heard, and ended at once, so what follows it does not wait for it
                l_Play->Ended = l_Mix.m_Mixed;
                l_Play->Done.store(true, std::memory_order_release);
            }
        }

        l_Mix.m_QueueHead.store(l_Head, std::memory_order_release);
        l_Mix.Seek();
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

            // The rest keep their order, so a play is read before any that starts once it ends, and it is let go of last, as the main thread may take it back at once
            std::copy(l_Mix.m_Active.begin() + it_Play + 1, l_Mix.m_Active.begin() + l_Mix.m_ActiveCount, l_Mix.m_Active.begin() + it_Play);
            --l_Mix.m_ActiveCount;
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

    // From the frame of the block its start falls on, or from where it would be by now when its start has passed, each frame read between the clip's two nearest samples, the one past its end being silence, and laid onto the mix's channels: a mono clip into each, into a mono mix the clip's channels averaged, and otherwise channel for channel. One that follows another's end waits for that end, as that one plays it. One whose curves move it ramps to what they give at each of the graph's reads, a pitched one moving through its clip at its pitch as it goes and playing until its clip runs out, and a filtered one through its low-pass, its cutoff moving evenly in octaves
    bool SoundEventMix::Render(SoundEventMixPlay& play, float* output, std::uint64_t frames, std::uint64_t clock)
    {
        const std::uint32_t l_Channels = play.Channels;
        float* l_Buffer = play.Buffer.data();
        ma_data_source* l_Source = play.Voice.GetDataSource();
        const auto a_Rate = [&play]() { return play.Rate * static_cast<double>(std::clamp(play.Pitch * play.LivePitch, SoundEventData::c_MinimumPitch, SoundEventData::c_MaximumPitch) / play.Pitch); };
        // The length a pitched play was foreseen to have no longer holds, so its clip says when it ends
        const bool l_Counted = !play.Pitched && play.Length != SoundEventPlayer::c_Never;
        if (!play.Begun)
        {
            if (play.After != nullptr)
            {
                if (play.After->Ended == std::numeric_limits<std::uint64_t>::max())
                {
                    return false;
                }

                play.Start = play.After->Ended + play.Offset;
            }

            if (play.Start >= clock + frames)
            {
                return false;
            }

            play.Begun = true;
            Follow(play, true);
            if (play.Start < clock)
            {
                play.Played = clock - play.Start;
                play.Position = static_cast<double>(play.Played) * (play.Pitched ? a_Rate() : play.Rate);
                const std::uint64_t l_Frame = static_cast<std::uint64_t>(play.Pitched ? play.Position : static_cast<double>(play.Played) * play.Rate);
                if ((l_Counted && play.Played >= play.Length) || play.Frames == 0 || (!play.Loop && l_Frame >= play.Frames))
                {
                    play.Ended = clock;

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

        if (play.Begun && play.Update != m_Update)
        {
            Follow(play, false);
        }

        const std::uint64_t l_First = play.Start > clock ? play.Start - clock : 0;
        std::uint64_t l_Count = frames - l_First;
        if (l_Counted)
        {
            l_Count = std::min(l_Count, play.Length - play.Played);
        }

        const std::uint32_t l_MixChannels = m_Channels;
        const float l_SampleRate = static_cast<float>(m_SampleRate);
        for (std::uint64_t it_Frame = 0; it_Frame < l_Count; ++it_Frame)
        {
            if (play.RampLeft > 0)
            {
                const float l_Left = static_cast<float>(play.RampLeft);
                play.LiveGain += (play.GainTo - play.LiveGain) / l_Left;
                play.LivePitch += (play.PitchTo - play.LivePitch) / l_Left;
                if (play.Filtered && ((play.RampFrames - play.RampLeft) % c_FilterStep == 0 || play.RampLeft == 1))
                {
                    const float l_Step = static_cast<float>(std::min(c_FilterStep, play.RampLeft)) / l_Left;
                    SetLowPass(play, play.RampLeft == 1 ? play.CutoffTo : play.Cutoff * std::pow(play.CutoffTo / play.Cutoff, l_Step), l_SampleRate);
                }

                if (--play.RampLeft == 0)
                {
                    play.LiveGain = play.GainTo;
                    play.LivePitch = play.PitchTo;
                }
            }

            const double l_Position = play.Pitched ? play.Position : static_cast<double>(play.Played) * play.Rate;
            const std::uint64_t l_Index = static_cast<std::uint64_t>(l_Position);
            const float l_Fraction = static_cast<float>(l_Position - static_cast<double>(l_Index));
            a_Fill(l_Index + 1);
            const std::uint64_t l_End = play.BufferStart + play.Buffered;
            if (l_Index < play.BufferStart || l_Index >= l_End)
            {
                play.Ended = clock + l_First + it_Frame;

                return true;
            }

            play.Played += 1;
            if (play.Pitched)
            {
                play.Position += a_Rate();
            }

            if (output == nullptr)
            {
                continue;
            }

            const float* l_Now = l_Buffer + (l_Index - play.BufferStart) * l_Channels;
            const float* l_After = l_Index + 1 < l_End ? l_Now + l_Channels : nullptr;
            const auto a_Sample = [&play, l_Now, l_After, l_Fraction](std::uint32_t channel)
            {
                const float l_Sample = l_Now[channel] + l_Fraction * ((l_After != nullptr ? l_After[channel] : 0.0f) - l_Now[channel]);

                return play.Filtered && channel < SoundEventMixPlay::c_MaxFiltered ? FilterSample(play, channel, l_Sample) : l_Sample;
            };

            const float l_Gain = play.Gain * play.LiveGain;
            float* l_Out = output + (l_First + it_Frame) * l_MixChannels;
            if (l_Channels == l_MixChannels)
            {
                for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                {
                    l_Out[it_Channel] += l_Gain * a_Sample(it_Channel);
                }
            }
            else if (l_Channels == 1)
            {
                const float l_Sample = l_Gain * a_Sample(0);
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

                l_Out[0] += l_Gain * l_Sum / static_cast<float>(l_Channels);
            }
            else
            {
                for (std::uint32_t it_Channel = 0; it_Channel < std::min(l_Channels, l_MixChannels); ++it_Channel)
                {
                    l_Out[it_Channel] += l_Gain * a_Sample(it_Channel);
                }
            }
        }

        const bool l_Done = l_Counted && play.Played >= play.Length;
        if (l_Done)
        {
            play.Ended = play.Start + play.Length;
        }

        return l_Done;
    }
}