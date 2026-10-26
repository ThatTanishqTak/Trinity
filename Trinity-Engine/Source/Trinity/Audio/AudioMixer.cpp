#include "Trinity/Audio/AudioMixer.hpp"

#include "Trinity/Audio/AudioEngine.hpp"
#include "Trinity/Audio/AudioEngineInternal.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Memory.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        // A meter's peak falls this fast once passed, and its RMS is over about this long
        constexpr float c_PeakFallDecibelsPerSecond = 20.0f;
        constexpr float c_RmsSeconds = 0.3f;

        // A gain moved from where it is to the main thread's latest over the mixer's ramp, each sample an even step of what is left, and, for a fader, the meter of what leaves it
        struct GainNode
        {
            ma_node_base Base{};
            // Set by the main thread and read by the audio thread each block
            std::atomic<float> TargetGain{ 1.0f };
            // The audio thread's alone once the node is in the graph
            float Gain = 1.0f;
            float RampGain = 1.0f;
            std::uint32_t RampLeft = 0;
            std::uint32_t RampFrames = 1;
            std::uint32_t Channels = 0;
            bool Metered = false;
            float PeakFall = 1.0f;
            float RmsCoefficient = 1.0f;
            std::array<float, AudioMeter::c_MaxChannels> Peak{};
            std::array<float, AudioMeter::c_MaxChannels> MeanSquare{};
            // Written by the audio thread after each block and read by any
            std::array<std::atomic<float>, AudioMeter::c_MaxChannels> PublishedPeak{};
            std::array<std::atomic<float>, AudioMeter::c_MaxChannels> PublishedMeanSquare{};
        };

        static_assert(std::is_standard_layout_v<GainNode>, "miniaudio reaches the node through its base, its first member.");

        // On the audio thread. A new gain starts a ramp from where the gain is, which ends exactly on it
        void ProcessGain(ma_node* node, const float** framesIn, [[maybe_unused]] ma_uint32* frameCountIn, float** framesOut, ma_uint32* frameCountOut)
        {
            GainNode& l_Node = *reinterpret_cast<GainNode*>(node);
            const float l_Target = l_Node.TargetGain.load(std::memory_order_relaxed);
            if (l_Target != l_Node.RampGain)
            {
                l_Node.RampGain = l_Target;
                l_Node.RampLeft = l_Node.RampFrames;
            }

            const ma_uint32 l_Frames = *frameCountOut;
            const std::uint32_t l_Channels = l_Node.Channels;
            const std::uint32_t l_Metered = l_Node.Metered ? std::min<std::uint32_t>(l_Channels, AudioMeter::c_MaxChannels) : 0;
            const float* l_In = framesIn[0];
            float* l_Out = framesOut[0];
            for (ma_uint32 it_Frame = 0; it_Frame < l_Frames; ++it_Frame)
            {
                if (l_Node.RampLeft > 0)
                {
                    l_Node.Gain += (l_Node.RampGain - l_Node.Gain) / static_cast<float>(l_Node.RampLeft);
                    if (--l_Node.RampLeft == 0)
                    {
                        l_Node.Gain = l_Node.RampGain;
                    }
                }

                const std::size_t l_Base = static_cast<std::size_t>(it_Frame) * l_Channels;
                for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                {
                    l_Out[l_Base + it_Channel] = l_In[l_Base + it_Channel] * l_Node.Gain;
                }

                for (std::uint32_t it_Channel = 0; it_Channel < l_Metered; ++it_Channel)
                {
                    const float l_Sample = l_Out[l_Base + it_Channel];
                    l_Node.Peak[it_Channel] = std::max(std::abs(l_Sample), l_Node.Peak[it_Channel] * l_Node.PeakFall);
                    l_Node.MeanSquare[it_Channel] += l_Node.RmsCoefficient * (l_Sample * l_Sample - l_Node.MeanSquare[it_Channel]);
                }
            }

            for (std::uint32_t it_Channel = 0; it_Channel < l_Metered; ++it_Channel)
            {
                l_Node.PublishedPeak[it_Channel].store(l_Node.Peak[it_Channel], std::memory_order_relaxed);
                l_Node.PublishedMeanSquare[it_Channel].store(l_Node.MeanSquare[it_Channel], std::memory_order_relaxed);
            }
        }

        // Both run whether or not anything plays into them, given silence: a fader so its meter falls to nothing, and a direct input so its group's resampler, when it has one, is always fed and never holds back a sample from before a silence
        constexpr ma_node_vtable c_DirectVTable{ &ProcessGain, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING };
        constexpr ma_node_vtable c_FaderVTable{ &ProcessGain, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING };

        // Only a pitch other than 1 takes the resampler, which runs at any pitch once there and holds the sound back by a sample
        bool IsPitched(float pitch)
        {
            return std::clamp(pitch, AudioMixerData::c_MinimumPitch, AudioMixerData::c_MaximumPitch) != 1.0f;
        }
    }

    // A group as it plays: what is sent to it straight into its direct input, which solo silences while the group only carries a soloed one, then its sound group, which every group below it plays into, then its fader, with its meter, into its parent's sound group
    struct AudioMixer::State
    {
        struct Group
        {
            // Apart from the group, so it can be made again with the resampler or without
            ma_sound_group* Sound = nullptr;
            GainNode Direct;
            GainNode Fader;
            Group* Parent = nullptr;
            bool Pitched = false;
            bool HasSound = false;
            bool HasDirect = false;
            bool HasFader = false;
            // Made by this Apply, so its gains start where they are to be, with no ramp
            bool Fresh = true;
            bool Kept = false;
        };

        // What a node was sent to, kept so it goes there again once the group is back, and where it plays now
        struct Destination
        {
            UUID GroupID;
            Group* Target = nullptr;
        };

        using GroupMap = std::unordered_map<UUID, Group*, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, Group*>, MemoryTag::Audio>>;
        using RouteMap = std::unordered_map<void*, Destination, std::hash<void*>, std::equal_to<void*>, TaggedAllocator<std::pair<void* const, Destination>, MemoryTag::Audio>>;

        explicit State(AudioEngine& engine) : TheEngine(AudioEngineInternal::GetEngine(engine))
        {

        }

        ~State();

        [[nodiscard]] Group* CreateGroup(bool pitched);
        void DestroyGroup(Group* group);
        [[nodiscard]] ma_sound_group* CreateSound(bool pitched);
        void SetPitched(Group& group, bool pitched);
        [[nodiscard]] bool InitGain(GainNode& node, bool metered);
        [[nodiscard]] Group* FindOrMaster(UUID id) const;
        void UpdateGains();

        ma_engine& TheEngine;
        AudioMixerData Data;
        Group* Master = nullptr;
        // Every group by its ID in the data, Master's included
        GroupMap Groups;
        RouteMap Routes;
    };

    AudioMixer::State::~State()
    {
        for (const auto& [it_ID, it_Group] : Groups)
        {
            DestroyGroup(it_Group);
        }
    }

    bool AudioMixer::State::InitGain(GainNode& node, bool metered)
    {
        const float l_SampleRate = static_cast<float>(ma_engine_get_sample_rate(&TheEngine));
        node.Channels = ma_engine_get_channels(&TheEngine);
        node.RampFrames = std::max<std::uint32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(c_RampMilliseconds) * ma_engine_get_sample_rate(&TheEngine) / 1000), 1);
        node.Metered = metered;
        node.PeakFall = std::pow(10.0f, -c_PeakFallDecibelsPerSecond / 20.0f / l_SampleRate);
        node.RmsCoefficient = 1.0f - std::exp(-1.0f / (c_RmsSeconds * l_SampleRate));

        const std::array<ma_uint32, 1> l_Channels{ node.Channels };
        ma_node_config l_Config = ma_node_config_init();
        l_Config.vtable = metered ? &c_FaderVTable : &c_DirectVTable;
        l_Config.pInputChannels = l_Channels.data();
        l_Config.pOutputChannels = l_Channels.data();

        return ma_node_init(ma_engine_get_node_graph(&TheEngine), &l_Config, &TheEngine.allocationCallbacks, &node.Base) == MA_SUCCESS;
    }

    // Its sound group starts in the engine's output for a moment, with nothing in it. None when it cannot be made
    ma_sound_group* AudioMixer::State::CreateSound(bool pitched)
    {
        ma_sound_group* l_Sound = Memory::New<ma_sound_group>(MemoryTag::Audio);
        if (ma_sound_group_init(&TheEngine, MA_SOUND_FLAG_NO_SPATIALIZATION | (pitched ? 0u : static_cast<ma_uint32>(MA_SOUND_FLAG_NO_PITCH)), nullptr, l_Sound) != MA_SUCCESS)
        {
            Memory::Delete(l_Sound);

            return nullptr;
        }

        return l_Sound;
    }

    // What plays into the old sound group, and what it plays into, move to the new one before the old goes, which moves the sound by the sample the resampler held back
    void AudioMixer::State::SetPitched(Group& group, bool pitched)
    {
        ma_sound_group* l_Sound = CreateSound(pitched);
        if (l_Sound == nullptr)
        {
            TR_CORE_CRITICAL("Audio: a mixer group could not be made");

            throw std::bad_alloc();
        }

        static_cast<void>(ma_node_attach_output_bus(l_Sound, 0, &group.Fader.Base, 0));
        static_cast<void>(ma_node_attach_output_bus(&group.Direct.Base, 0, l_Sound, 0));
        for (const auto& [it_ID, it_Child] : Groups)
        {
            if (it_Child->Parent == &group)
            {
                static_cast<void>(ma_node_attach_output_bus(&it_Child->Fader.Base, 0, l_Sound, 0));
            }
        }

        ma_sound_group_uninit(group.Sound);
        Memory::Delete(group.Sound);
        group.Sound = l_Sound;
        group.Pitched = pitched;
    }

    // Wired within itself and to nothing above, so the audio thread reaches none of it until Apply has set its gains and plays it into its parent. Only running out of memory fails, which Memory treats the same way
    AudioMixer::State::Group* AudioMixer::State::CreateGroup(bool pitched)
    {
        Group* l_Group = Memory::New<Group>(MemoryTag::Audio);
        l_Group->Sound = CreateSound(pitched);
        l_Group->Pitched = pitched;
        l_Group->HasSound = l_Group->Sound != nullptr;
        l_Group->HasDirect = l_Group->HasSound && InitGain(l_Group->Direct, false);
        l_Group->HasFader = l_Group->HasDirect && InitGain(l_Group->Fader, true);
        if (!l_Group->HasFader)
        {
            TR_CORE_CRITICAL("Audio: a mixer group could not be made");
            DestroyGroup(l_Group);

            throw std::bad_alloc();
        }

        static_cast<void>(ma_node_attach_output_bus(l_Group->Sound, 0, &l_Group->Fader.Base, 0));
        static_cast<void>(ma_node_attach_output_bus(&l_Group->Direct.Base, 0, l_Group->Sound, 0));

        return l_Group;
    }

    // Whatever plays into it is let go with it
    void AudioMixer::State::DestroyGroup(Group* group)
    {
        if (group->HasDirect)
        {
            ma_node_uninit(&group->Direct.Base, &TheEngine.allocationCallbacks);
        }

        if (group->HasSound)
        {
            ma_sound_group_uninit(group->Sound);
            Memory::Delete(group->Sound);
        }

        if (group->HasFader)
        {
            ma_node_uninit(&group->Fader.Base, &TheEngine.allocationCallbacks);
        }

        Memory::Delete(group);
    }

    AudioMixer::State::Group* AudioMixer::State::FindOrMaster(UUID id) const
    {
        const auto a_Found = id ? Groups.find(id) : Groups.end();

        return a_Found != Groups.end() ? a_Found->second : Master;
    }

    // As Unity's mixer has it: mute silences a group and so everything below it, and while any group is soloed, a group neither soloed nor below a soloed one is silent unless one below it is soloed, when it carries only that. A group made by this Apply takes its gains at once
    void AudioMixer::State::UpdateGains()
    {
        const bool l_AnySolo = std::ranges::any_of(Data.Groups, &AudioMixerGroup::Solo);
        for (const AudioMixerGroup& it_Group : Data.Groups)
        {
            bool l_Soloed = false;
            for (const AudioMixerGroup* it_Above = &it_Group; it_Above != nullptr && !l_Soloed; it_Above = Data.FindGroup(it_Above->Parent))
            {
                l_Soloed = it_Above->Solo;
            }

            bool l_Carries = false;
            for (const AudioMixerGroup& it_Other : Data.Groups)
            {
                for (const AudioMixerGroup* it_Above = it_Other.Solo ? Data.FindGroup(it_Other.Parent) : nullptr; it_Above != nullptr && !l_Carries; it_Above = Data.FindGroup(it_Above->Parent))
                {
                    l_Carries = it_Above->ID == it_Group.ID;
                }
            }

            const bool l_Silenced = it_Group.Mute || (l_AnySolo && !l_Soloed && !l_Carries);
            const float l_Fader = l_Silenced ? 0.0f : AudioDecibelsToGain(it_Group.Volume);
            const float l_Direct = l_AnySolo && !l_Soloed ? 0.0f : 1.0f;
            Group& l_Group = *Groups.at(it_Group.ID);
            for (const auto& [it_Node, it_Gain] : { std::pair(&l_Group.Fader, l_Fader), std::pair(&l_Group.Direct, l_Direct) })
            {
                if (l_Group.Fresh)
                {
                    it_Node->Gain = it_Gain;
                    it_Node->RampGain = it_Gain;
                }

                it_Node->TargetGain.store(it_Gain, std::memory_order_relaxed);
            }

            if (l_Group.Pitched != IsPitched(it_Group.Pitch))
            {
                SetPitched(l_Group, IsPitched(it_Group.Pitch));
            }

            ma_sound_group_set_pitch(l_Group.Sound, std::clamp(it_Group.Pitch, AudioMixerData::c_MinimumPitch, AudioMixerData::c_MaximumPitch));
            l_Group.Fresh = false;
        }
    }

    // Master alone, into the engine's output
    AudioMixer::AudioMixer(AudioEngine& engine) : m_State(Memory::New<State>(MemoryTag::Audio, engine))
    {
        State& l_State = *m_State;
        l_State.Data = AudioMixerData::MakeDefault();
        l_State.Master = l_State.CreateGroup(false);
        l_State.Groups.emplace(l_State.Data.GetMaster().ID, l_State.Master);
        l_State.UpdateGains();
        static_cast<void>(ma_node_attach_output_bus(&l_State.Master->Fader.Base, 0, &AudioEngineInternal::GetMaster(engine), 0));
    }

    AudioMixer::~AudioMixer()
    {
        Memory::Delete(m_State);
    }

    // The groups are made, kept and wired before any is let go, so a group that leaves takes nothing with it that stays: what was sent to it has moved to Master, and the groups below it that stay have moved to their new parents
    bool AudioMixer::Apply(const AudioMixerData& data)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A mixer changes on the main thread.");

        if (const std::string l_Error = ValidateAudioMixer(data); !l_Error.empty())
        {
            TR_CORE_ERROR("Audio: the mixer is left as it was, since {}", l_Error);

            return false;
        }

        State& l_State = *m_State;
        if (data == l_State.Data)
        {
            return true;
        }

        for (const auto& [it_ID, it_Group] : l_State.Groups)
        {
            it_Group->Kept = false;
        }

        State::GroupMap l_Groups;
        for (const AudioMixerGroup& it_Group : data.Groups)
        {
            State::Group* l_Group = l_State.Master;
            if (it_Group.Parent)
            {
                const auto a_Old = l_State.Groups.find(it_Group.ID);
                l_Group = a_Old != l_State.Groups.end() && a_Old->second != l_State.Master ? a_Old->second : l_State.CreateGroup(IsPitched(it_Group.Pitch));
            }

            l_Group->Kept = true;
            l_Groups.emplace(it_Group.ID, l_Group);
        }

        std::vector<State::Group*, TaggedAllocator<State::Group*, MemoryTag::Audio>> l_Gone;
        for (const auto& [it_ID, it_Group] : l_State.Groups)
        {
            if (!it_Group->Kept)
            {
                l_Gone.push_back(it_Group);
            }
        }

        // A copy of its own, so a smaller mixer lets go of what a larger one held
        l_State.Groups = std::move(l_Groups);
        l_State.Data = AudioMixerData(data);
        l_State.UpdateGains();

        for (const AudioMixerGroup& it_Group : data.Groups)
        {
            State::Group& l_Group = *l_State.Groups.at(it_Group.ID);
            State::Group* l_Parent = it_Group.Parent ? l_State.Groups.at(it_Group.Parent) : nullptr;
            if (l_Parent != nullptr && l_Group.Parent != l_Parent)
            {
                static_cast<void>(ma_node_attach_output_bus(&l_Group.Fader.Base, 0, l_Parent->Sound, 0));
                l_Group.Parent = l_Parent;
            }
        }

        for (auto& [it_Node, it_Route] : l_State.Routes)
        {
            State::Group* l_Target = l_State.FindOrMaster(it_Route.GroupID);
            if (l_Target != it_Route.Target)
            {
                static_cast<void>(ma_node_attach_output_bus(static_cast<ma_node*>(it_Node), 0, &l_Target->Direct.Base, 0));
                it_Route.Target = l_Target;
            }
        }

        for (State::Group* it_Group : l_Gone)
        {
            l_State.DestroyGroup(it_Group);
        }

        return true;
    }

    const AudioMixerData& AudioMixer::GetData() const
    {
        return m_State->Data;
    }

    std::optional<AudioMeter> AudioMixer::GetMeter(UUID group) const
    {
        const auto a_Found = group ? m_State->Groups.find(group) : m_State->Groups.end();
        if (a_Found == m_State->Groups.end())
        {
            return std::nullopt;
        }

        const GainNode& l_Fader = a_Found->second->Fader;
        AudioMeter l_Meter;
        l_Meter.Channels = std::min<std::uint32_t>(l_Fader.Channels, AudioMeter::c_MaxChannels);
        for (std::uint32_t it_Channel = 0; it_Channel < l_Meter.Channels; ++it_Channel)
        {
            l_Meter.Peak[it_Channel] = l_Fader.PublishedPeak[it_Channel].load(std::memory_order_relaxed);
            l_Meter.Rms[it_Channel] = std::sqrt(std::max(l_Fader.PublishedMeanSquare[it_Channel].load(std::memory_order_relaxed), 0.0f));
        }

        return l_Meter;
    }

    void AudioMixer::Route(void* node, UUID group)
    {
        State& l_State = *m_State;
        State::Group* l_Target = l_State.FindOrMaster(group);
        const auto [a_Route, a_New] = l_State.Routes.try_emplace(node);
        a_Route->second.GroupID = group;
        if (a_New || a_Route->second.Target != l_Target)
        {
            static_cast<void>(ma_node_attach_output_bus(static_cast<ma_node*>(node), 0, &l_Target->Direct.Base, 0));
            a_Route->second.Target = l_Target;
        }
    }

    // An emptied map keeps its buckets, so it is replaced, and Audio reads as it did before the first sound
    void AudioMixer::Unroute(void* node)
    {
        State& l_State = *m_State;
        l_State.Routes.erase(node);
        if (l_State.Routes.empty())
        {
            State::RouteMap().swap(l_State.Routes);
        }
    }
}