#pragma once

#include "Trinity/Audio/AudioClipStorage.hpp"
#include "Trinity/Audio/SoundEventData.hpp"
#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <miniaudio.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Trinity
{
    // A Sound Event as its instances play it: a copy of its data, and for each container its parent, the place of its children among them all, its clip among the event's clips, a Switch's parameter, a Switch's child's label, its curves, and whether curves of its own or above it move its volume or pitch or filter it. Made once for each version of an asset and shared by every instance playing it, so an instance plays on as the version it began with when the asset reloads
    struct SoundEventPlan
    {
        static constexpr std::uint32_t c_None = std::numeric_limits<std::uint32_t>::max();

        struct Node
        {
            std::uint32_t Parent = c_None;
            std::uint32_t FirstChild = 0;
            std::uint32_t ChildCount = 0;
            std::uint32_t Clip = c_None;
            std::uint32_t Parameter = c_None;
            std::uint32_t Case = c_None;
            std::uint32_t FirstCurve = 0;
            std::uint32_t CurveCount = 0;
            bool Faded = false;
            bool Pitched = false;
            bool Filtered = false;
        };

        // A container's curve, by its parameter's place among the event's and its own among the container's
        struct Curve
        {
            std::uint32_t Parameter = 0;
            std::uint32_t Container = 0;
            std::uint32_t Index = 0;
        };

        // What the curves give a container, and so each play below it, at the parameters' values: a gain and a pitch its own are multiplied by, and the cutoff of its low-pass
        struct NodeValue
        {
            float Gain = 1.0f;
            float Pitch = 1.0f;
            float Cutoff = SoundEventData::c_MaximumCutoff;
        };

        UUID Event;
        std::uint64_t Serial = 0;
        SoundEventData Data;
        std::vector<Node, TaggedAllocator<Node, MemoryTag::Audio>> Nodes;
        // Each container's children in their order, one after another
        std::vector<std::uint32_t, TaggedAllocator<std::uint32_t, MemoryTag::Audio>> Children;
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> Clips;
        // Each container's curves, one after another
        std::vector<Curve, TaggedAllocator<Curve, MemoryTag::Audio>> Curves;
        bool HasPitchCurves = false;

        [[nodiscard]] static Ref<const SoundEventPlan> Make(UUID event, const SoundEventAsset& asset);

        // Each container's values, the parameters' given in the event's order, a curve's value at its nearest end beyond its range. Allocates nothing, so the audio thread works its mix's out as it reads
        void Evaluate(std::span<const float> values, std::span<NodeValue> nodes) const;
    };

    // A parameter's value as the game gives it, read at frames of the engine's clock: the value from the frame its change starts on, and before that the one it had, so every instance reading it at one frame agrees, however a change falls across their reads. Until one is given it reads as each instance's default. Written by the main thread alone through a sequence lock, so neither thread waits
    class SoundParameterTarget
    {
    public:
        struct Reading
        {
            float Value = 0.0f;
            float Before = 0.0f;
            std::uint64_t From = 0;
            bool Given = false;
            bool GivenBefore = false;

            // None where nothing is given
            [[nodiscard]] std::optional<float> At(std::uint64_t frame) const;
        };

        // Main thread: from the frame given, which the audio thread is yet to reach, and so no sooner than the frame of the change before
        void Set(float value, std::uint64_t frame);
        void Clear(std::uint64_t frame);
        // Main thread
        [[nodiscard]] const Reading& GetWritten() const { return m_Written; }
        // Any thread. False while the main thread writes it, and the reading as it was
        [[nodiscard]] bool Read(Reading& reading) const;

    private:
        void Write(float value, bool given, std::uint64_t frame);

        std::atomic<std::uint32_t> m_Sequence{ 0 };
        std::atomic<float> m_Value{ 0.0f };
        std::atomic<float> m_Before{ 0.0f };
        std::atomic<std::uint64_t> m_From{ 0 };
        std::atomic<bool> m_Given{ false };
        std::atomic<bool> m_GivenBefore{ false };
        Reading m_Written;
    };

    // Where a parameter is at one frame of the engine's clock, from where it was at an earlier one, having sought towards its target as the target reads at each frame between, at its speed and the engine's rate. A discrete one, and one of speed 0, is at its target, and a target of none is its default
    [[nodiscard]] float SeekSoundParameter(const SoundParameter& parameter, float value, const SoundParameterTarget::Reading& target, std::uint64_t from, std::uint64_t to, double sampleRate);

    // A clip as an event's instances time it: how many frames it has and at what rate, none for one that failed, is empty or is no clip, which plays as nothing
    struct SoundEventClip
    {
        std::uint64_t Frames = 0;
        std::uint32_t SampleRate = 0;
    };

    // What an AudioSource keeps of the events it plays from one play to the next, as Unity 6's AudioSource keeps its Audio Random Container's: how often it has played, which seeds each play, the last picks of each Random it has played, so Avoid Repeat spans its plays, and the values its parameters were given, which hold for its later plays. Grows only for a Random or a parameter it has not had before, and room is made for an event's parameters as it starts, so setting them allocates nothing
    struct SoundEventMemory
    {
        static constexpr std::size_t c_MaxAvoid = 32;

        struct History
        {
            UUID Container;
            // The children picked, by their place among the Random's, the latest at Next - 1
            std::array<std::uint32_t, c_MaxAvoid> Picks{};
            std::uint32_t Next = 0;
            std::uint32_t Count = 0;

            [[nodiscard]] bool WasPicked(std::uint32_t child, std::uint32_t last) const;
            void Add(std::uint32_t child);
        };

        struct Value
        {
            std::uint64_t Hash = 0;
            TaggedString<MemoryTag::Audio> Name;
            float Number = 0.0f;
            bool Given = true;
        };

        std::uint64_t Plays = 0;
        std::vector<History, TaggedAllocator<History, MemoryTag::Audio>> Histories;
        std::vector<Value, TaggedAllocator<Value, MemoryTag::Audio>> Values;

        [[nodiscard]] History& FindHistory(UUID container);
        [[nodiscard]] std::optional<float> FindValue(std::string_view name) const;
        void SetValue(std::string_view name, float number);
        // Room for a value by that name, none given yet
        void Reserve(std::string_view name);
    };

    // xoshiro256** seeded through SplitMix64, so the same seed gives the same choices on every platform
    class SoundEventRandom
    {
    public:
        explicit SoundEventRandom(std::uint64_t seed = 0);

        // The seed of a play: the event, the entity playing it and how often that entity has played, mixed so nearby numbers give unrelated seeds
        [[nodiscard]] static std::uint64_t MakeSeed(UUID event, UUID entity, std::uint64_t plays);

        [[nodiscard]] std::uint64_t Next();
        // From 0 up to but never 1
        [[nodiscard]] float NextFloat();

    private:
        std::array<std::uint64_t, 4> m_State{};
    };

    // One playing instance's schedule, on the main thread, in the instance's frames at the engine's rate: each container started as its parent starts it, after its delay, at a volume and pitch varied at random from its parent's, and each clip a play starting on an exact frame, a Sequence's next on the frame the one before ends, as its length at its rate comes to. Only what starts before a horizon a little ahead is scheduled, so a Switch picks by its parameter as it is when it is triggered, give or take that much, and an event that goes on for good is scheduled as it goes. A Random's picks come from the instance's seed and the source's memory of its last picks, so the same seed and memory give the same plays on every run, whether the instance is heard or not. In an event whose curves move a pitch, a clip's length is only foreseen, at its pitch as it goes, so what starts once a clip ends, and so on after it, is given that clip, which its mix starts it after where it really ends
    class SoundEventPlayer
    {
    public:
        static constexpr std::uint64_t c_Never = std::numeric_limits<std::uint64_t>::max();
        static constexpr std::uint32_t c_NoPlay = std::numeric_limits<std::uint32_t>::max();
        // A Random triggers itself no more often than this, so an interval of 0 cannot fill a frame with plays
        static constexpr float c_MinimumInterval = 0.01f;

        struct Play
        {
            // Its clip, by its place among the plan's clips
            std::uint32_t Clip = 0;
            std::uint64_t Start = 0;
            // c_Never for one that loops
            std::uint64_t Length = 0;
            // The clip's frames it moves through for each of the instance's, at the pitch it was given, and that pitch
            double Rate = 1.0;
            float Gain = 1.0f;
            bool Loop = false;
            float Pitch = 1.0f;
            // Its container's place among the plan's
            std::uint32_t Node = 0;
            // From 0 in the order the instance's plays start, and the earlier one it starts Offset frames after the end of, if any
            std::uint32_t Number = 0;
            std::uint32_t After = c_NoPlay;
            std::uint64_t Offset = 0;

            [[nodiscard]] std::uint64_t GetEnd() const { return Length == c_Never ? c_Never : Start + Length; }
        };

        using PlayList = std::vector<Play, TaggedAllocator<Play, MemoryTag::Audio>>;

        // The clips in the plan's order. The root starts at frame 0
        SoundEventPlayer(Ref<const SoundEventPlan> plan, std::span<const SoundEventClip> clips, std::uint64_t seed, std::uint32_t sampleRate);

        // Starts everything whose time comes before the horizon, appending a play for each clip, in the order they start. A Switch picks by the parameter's value given, in the event's order, or else as the memory has it
        void Schedule(std::uint64_t horizon, SoundEventMemory& memory, PlayList& plays, std::span<const float> values = {});
        // Before a Schedule, in an event whose curves move a pitch: the pitch each container's curves give it now, at the instance's frame given, from which each clip playing is foreseen to end at that pitch, its play in the list given lengthened or shortened to match
        void Retime(std::span<const float> pitches, std::uint64_t clock, PlayList& plays);
        // As its asset is edited, with the new version's clips in its plan's order: what it has started plays on, and what each container starts from its next trigger is the new version's, each container found by its ID. A container started that the new version lacks, or has as another type, ends at the frame given with everything started below it, its parent hearing of it as of a child ending then
        void Adopt(Ref<const SoundEventPlan> plan, std::span<const SoundEventClip> clips, std::uint64_t time);


        // Whether nothing is left to start or to end, every container having ended by the frame GetEnd gives, which is c_Never until then
        [[nodiscard]] bool IsFinished() const;
        [[nodiscard]] std::uint64_t GetEnd() const { return m_End; }
        [[nodiscard]] const SoundEventPlan& GetPlan() const { return *m_Plan; }

    private:
        static constexpr std::uint32_t c_NoActivation = std::numeric_limits<std::uint32_t>::max();

        // A container started, until it ends, and in an event whose curves move a pitch the play its start follows the end of, foreseen at a frame, and the play its own end is, foreseen likewise, its anchor's until a play of its own ends later
        struct Activation
        {
            std::uint32_t Node = 0;
            std::uint32_t Parent = c_NoActivation;
            float Gain = 1.0f;
            float Pitch = 1.0f;
            // When it ends at the latest of its children so far
            std::uint64_t End = 0;
            // Its children playing
            std::uint32_t Running = 0;
            // A Sequence's child playing, or how often a Random has triggered
            std::uint32_t Step = 0;
            // When a Sequence's round began, or a Random last triggered
            std::uint64_t Last = 0;
            bool TriggerPending = false;
            std::uint32_t Anchor = c_NoPlay;
            std::uint64_t AnchorTime = 0;
            std::uint32_t EndPlay = c_NoPlay;
            std::uint64_t EndPlayTime = 0;
        };

        // A clip playing whose pitch curves move, as far as it is foreseen to have got, in the clip's frames, at the instance's frame it got there, at the pitch its curves gave it then, and the action that tells its parent it has ended, or the instance's end for a clip that is the root
        struct Pitched
        {
            std::uint32_t Number = 0;
            std::uint32_t Node = 0;
            std::uint32_t Parent = c_NoActivation;
            std::uint64_t Order = 0;
            std::uint64_t Start = 0;
            std::uint64_t End = 0;
            double Frames = 0.0;
            double ClipRate = 1.0;
            float Pitch = 1.0f;
            double Position = 0.0;
            std::uint64_t Clock = 0;
            float Live = 1.0f;
        };

        enum class ActionKind : std::uint8_t
        {
            Begin,
            ChildEnded,
            Trigger
        };

        // Ordered by its time, then by the order it was made in, so ties go the same way on every run. A child's end carries the play that is its end, foreseen at a frame
        struct Action
        {
            std::uint64_t Time = 0;
            std::uint64_t Order = 0;
            std::uint32_t Activation = 0;
            ActionKind Kind = ActionKind::Begin;
            std::uint32_t Play = c_NoPlay;
            std::uint64_t PlayTime = 0;
        };

        void Activate(std::uint32_t node, std::uint32_t parent, std::uint64_t time, std::uint32_t anchor, std::uint64_t anchorTime);
        void Begin(std::uint32_t activation, std::uint64_t time);
        void Trigger(std::uint32_t activation, std::uint64_t time);
        void ChildEnded(const Action& action);
        // The order of the action it pushes, or none for the root
        std::uint64_t End(std::uint32_t activation, std::uint64_t time);
        std::uint64_t Push(std::uint64_t time, std::uint32_t activation, ActionKind kind, std::uint32_t play = c_NoPlay, std::uint64_t playTime = 0);
        [[nodiscard]] std::uint32_t Pick(const Activation& activation);
        [[nodiscard]] std::uint64_t DrawInterval(const SoundContainer& container);
        [[nodiscard]] std::uint64_t ToFrames(float seconds) const;
        // The clip's frames a play moves through for each of the instance's, at its pitch and its curves'
        [[nodiscard]] static double GetLiveRate(double clipRate, float pitch, float live);

        Ref<const SoundEventPlan> m_Plan;
        std::vector<SoundEventClip, TaggedAllocator<SoundEventClip, MemoryTag::Audio>> m_Clips;
        SoundEventRandom m_Random;
        double m_SampleRate = 48000.0;
        std::vector<Activation, TaggedAllocator<Activation, MemoryTag::Audio>> m_Activations;
        std::vector<std::uint32_t, TaggedAllocator<std::uint32_t, MemoryTag::Audio>> m_Free;
        std::vector<Action, TaggedAllocator<Action, MemoryTag::Audio>> m_Actions;
        std::uint64_t m_NextOrder = 0;
        std::uint64_t m_End = c_Never;
        bool m_Ended = false;
        // An event whose curves move a pitch: the pitch each container's give it, as last told, and its clips playing
        bool m_Anchored = false;
        std::vector<float, TaggedAllocator<float, MemoryTag::Audio>> m_Pitches;
        std::vector<Pitched, TaggedAllocator<Pitched, MemoryTag::Audio>> m_Running;
        std::uint32_t m_NextPlay = 0;
        // Set for the length of a Schedule
        SoundEventMemory* m_Memory = nullptr;
        PlayList* m_Plays = nullptr;
        std::span<const float> m_Values;
    };

    // A play of a clip as an instance's mix reads it: the main thread starts its voice, hands it to the mix and takes it back once the mix is done with it, and the rest is the audio thread's meanwhile. One that starts after another's end is handed that one, which the main thread keeps until this one is done with it too
    struct SoundEventMixPlay
    {
        // A low-pass curve filters this many of its clip's channels at most, which 7.1 needs, and any further channel passes clear
        static constexpr std::size_t c_MaxFiltered = 8;

        AudioClipVoice Voice;
        std::uint64_t Start = 0;
        std::uint64_t Length = 0;
        double Rate = 1.0;
        float Gain = 1.0f;
        std::uint32_t Channels = 1;
        // The clip's, which a play that loops comes back to the start at
        std::uint64_t Frames = 0;
        bool Loop = false;
        float Pitch = 1.0f;
        std::uint32_t Node = 0;
        std::uint32_t Number = 0;
        SoundEventMixPlay* After = nullptr;
        std::uint64_t Offset = 0;
        // Whether its container's curves, or those above it, move its volume or pitch or filter it. A pitched one plays until its clip runs out, whatever its length
        bool Faded = false;
        bool Pitched = false;
        bool Filtered = false;
        std::vector<float, TaggedAllocator<float, MemoryTag::Audio>> Buffer;
        // The instance's frame it ended on, set before Done
        std::uint64_t Ended = std::numeric_limits<std::uint64_t>::max();
        std::atomic<bool> Done{ false };

        // The audio thread's: the instance's frames it has played, where a pitched one is in its clip, and the clip's frames from the play's start that its buffer holds
        bool Begun = false;
        std::uint64_t Played = 0;
        double Position = 0.0;
        std::uint64_t BufferStart = 0;
        std::uint64_t Buffered = 0;
        bool InputEnded = false;
        // The audio thread's: its curves' gain, pitch and cutoff as it plays them, each moving evenly to the mix's latest over the frames left, the latest it has taken, its low-pass's coefficients, normalized by a0, and two registers for each channel, in transposed direct form II
        std::uint32_t Update = 0;
        float LiveGain = 1.0f;
        float GainTo = 1.0f;
        float LivePitch = 1.0f;
        float PitchTo = 1.0f;
        float Cutoff = 0.0f;
        float CutoffTo = 0.0f;
        std::uint32_t RampLeft = 0;
        std::uint32_t RampFrames = 1;
        float B0 = 1.0f;
        float B1 = 0.0f;
        float B2 = 0.0f;
        float A1 = 0.0f;
        float A2 = 0.0f;
        std::array<std::array<float, 2>, c_MaxFiltered> Registers{};
    };

    // An instance's plays mixed together, as a data source of miniaudio's that a sound reads as it would a clip, so the instance is one voice, heard from where its source is. Each play starts on the frame of the instance's clock it was given, or after the end of the play it was given, as that play really ends, its clip read at its rate between samples, so the plays of a Layer start on one sample and a Sequence's follow each other with no gap. A play given to it after its frame has passed starts where it would be by now. It reads silence while it waits for its next play, and ends once it has been told no more are coming and its last has played. Given its event's parameters, it seeks each towards its target as the engine's graph reads it, so every instance reading in one period hears a change in it, and its plays move to what their curves give over a period's frames. The main thread hands plays over through a queue, and the audio thread never allocates, locks or waits
    class SoundEventMix
    {
    public:
        static constexpr std::uint32_t c_MaxPlays = 64;
        // The clip's frames a play reads ahead
        static constexpr std::uint32_t c_BufferFrames = 512;
        // A curve's change ramps over the frames since the graph last read, and no more than this
        static constexpr std::uint32_t c_MaxRampFrames = 4096;

        SoundEventMix(std::uint32_t channels, std::uint32_t sampleRate, std::uint64_t clock);
        ~SoundEventMix();

        SoundEventMix(const SoundEventMix&) = delete;
        SoundEventMix& operator=(const SoundEventMix&) = delete;

        // Main thread, before anything reads it: the plan, which outlives it, each parameter's target, the instance's own or a global one, which do too, and where each parameter is at the engine's frame given, on the graph whose time it reads
        void Control(const SoundEventPlan& plan, std::span<const SoundParameterTarget* const> targets, std::span<const float> values, const ma_node_graph* graph, std::uint64_t frame);
        // Main thread: where a parameter is as the audio thread last sought it
        [[nodiscard]] float GetValue(std::size_t parameter) const;

        [[nodiscard]] ma_data_source* GetDataSource() { return &m_Source.Base; }
        // Main thread. False while the queue is full, so the play is offered again later
        [[nodiscard]] bool Offer(SoundEventMixPlay& play);
        // Main thread: no play is offered after this
        void Finish();
        // The instance's frames mixed so far
        [[nodiscard]] std::uint64_t GetClock() const { return m_Clock.load(std::memory_order_acquire); }

    private:
        struct Source
        {
            ma_data_source_base Base;
            SoundEventMix* Mix;
        };

        static ma_result OnRead(ma_data_source* source, void* frames, ma_uint64 frameCount, ma_uint64* framesRead);
        static ma_result OnSeek(ma_data_source* source, ma_uint64 frame);
        static ma_result OnGetDataFormat(ma_data_source* source, ma_format* format, ma_uint32* channels, ma_uint32* sampleRate, ma_channel* channelMap, size_t channelMapCapacity);
        static ma_result OnGetCursor(ma_data_source* source, ma_uint64* cursor);
        static ma_result OnSetLooping(ma_data_source* source, ma_bool32 looping);

        // Once for each time of the graph, before the plays are read: each parameter sought to where it is by then, and the curves' values from there
        void Seek();
        // A play takes the latest values, ramping to them from where it is, or from them at once as it begins
        void Follow(SoundEventMixPlay& play, bool begin) const;
        // Whether the play is done
        bool Render(SoundEventMixPlay& play, float* output, std::uint64_t frames, std::uint64_t clock);

        Source m_Source{};
        std::uint32_t m_Channels = 1;
        std::uint32_t m_SampleRate = 48000;
        std::atomic<std::uint64_t> m_Clock{ 0 };
        std::atomic<bool> m_Finished{ false };
        // A queue of one writer, the main thread, and one reader, the audio thread
        std::array<SoundEventMixPlay*, c_MaxPlays> m_Queue{};
        std::atomic<std::uint32_t> m_QueueHead{ 0 };
        std::atomic<std::uint32_t> m_QueueTail{ 0 };
        // The audio thread's
        std::array<SoundEventMixPlay*, c_MaxPlays> m_Active{};
        std::uint32_t m_ActiveCount = 0;
        std::uint64_t m_Mixed = 0;
        // Once controlled: the event's parameters as the audio thread seeks them, with what it last read of their targets, published for the main thread, its containers' values from them, the graph's time they were sought to, how often they have been, and the frames the latest ramps over
        const SoundEventPlan* m_Plan = nullptr;
        const ma_node_graph* m_Graph = nullptr;
        std::vector<const SoundParameterTarget*, TaggedAllocator<const SoundParameterTarget*, MemoryTag::Audio>> m_Targets;
        std::vector<SoundParameterTarget::Reading, TaggedAllocator<SoundParameterTarget::Reading, MemoryTag::Audio>> m_Readings;
        std::vector<float, TaggedAllocator<float, MemoryTag::Audio>> m_Values;
        std::vector<std::atomic<float>, TaggedAllocator<std::atomic<float>, MemoryTag::Audio>> m_Published;
        std::vector<SoundEventPlan::NodeValue, TaggedAllocator<SoundEventPlan::NodeValue, MemoryTag::Audio>> m_Nodes;
        std::uint64_t m_Frame = 0;
        std::uint32_t m_Update = 0;
        std::uint32_t m_RampFrames = 1;
    };
}