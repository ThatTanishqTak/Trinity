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
    // A Sound Event as its instances play it: a copy of its data, and for each container the place of its children among them all, its clip among the event's clips, a Switch's parameter, and a Switch's child's label. Made once for each version of an asset and shared by every instance playing it, so an instance plays on as the version it began with when the asset reloads
    struct SoundEventPlan
    {
        static constexpr std::uint32_t c_None = std::numeric_limits<std::uint32_t>::max();

        struct Node
        {
            std::uint32_t FirstChild = 0;
            std::uint32_t ChildCount = 0;
            std::uint32_t Clip = c_None;
            std::uint32_t Parameter = c_None;
            std::uint32_t Case = c_None;
        };

        UUID Event;
        std::uint64_t Serial = 0;
        SoundEventData Data;
        std::vector<Node, TaggedAllocator<Node, MemoryTag::Audio>> Nodes;
        // Each container's children in their order, one after another
        std::vector<std::uint32_t, TaggedAllocator<std::uint32_t, MemoryTag::Audio>> Children;
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> Clips;

        [[nodiscard]] static Ref<const SoundEventPlan> Make(UUID event, const SoundEventAsset& asset);
    };

    // A clip as an event's instances time it: how many frames it has and at what rate, none for one that failed, is empty or is no clip, which plays as nothing
    struct SoundEventClip
    {
        std::uint64_t Frames = 0;
        std::uint32_t SampleRate = 0;
    };

    // What an AudioSource keeps of the events it plays from one play to the next, as Unity 6's AudioSource keeps its Audio Random Container's: how often it has played, which seeds each play, the last picks of each Random it has played, so Avoid Repeat spans its plays, and the values its parameters were given, which hold for its later plays. Grows only for a Random or a parameter it has not had before
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
        };

        std::uint64_t Plays = 0;
        std::vector<History, TaggedAllocator<History, MemoryTag::Audio>> Histories;
        std::vector<Value, TaggedAllocator<Value, MemoryTag::Audio>> Values;

        [[nodiscard]] History& FindHistory(UUID container);
        [[nodiscard]] std::optional<float> FindValue(std::string_view name) const;
        void SetValue(std::string_view name, float number);
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

    // One playing instance's schedule, on the main thread, in the instance's frames at the engine's rate: each container started as its parent starts it, after its delay, at a volume and pitch varied at random from its parent's, and each clip a play starting on an exact frame, a Sequence's next on the frame the one before ends, as its length at its rate comes to. Only what starts before a horizon a little ahead is scheduled, so a Switch picks by its parameter as it is when it is triggered, give or take that much, and an event that goes on for good is scheduled as it goes. A Random's picks come from the instance's seed and the source's memory of its last picks, so the same seed and memory give the same plays on every run, whether the instance is heard or not
    class SoundEventPlayer
    {
    public:
        static constexpr std::uint64_t c_Never = std::numeric_limits<std::uint64_t>::max();
        // A Random triggers itself no more often than this, so an interval of 0 cannot fill a frame with plays
        static constexpr float c_MinimumInterval = 0.01f;

        struct Play
        {
            // Its clip, by its place among the plan's clips
            std::uint32_t Clip = 0;
            std::uint64_t Start = 0;
            // c_Never for one that loops
            std::uint64_t Length = 0;
            // The clip's frames it moves through for each of the instance's
            double Rate = 1.0;
            float Gain = 1.0f;
            bool Loop = false;

            [[nodiscard]] std::uint64_t GetEnd() const { return Length == c_Never ? c_Never : Start + Length; }
        };

        using PlayList = std::vector<Play, TaggedAllocator<Play, MemoryTag::Audio>>;

        // The clips in the plan's order. The root starts at frame 0
        SoundEventPlayer(Ref<const SoundEventPlan> plan, std::span<const SoundEventClip> clips, std::uint64_t seed, std::uint32_t sampleRate);

        // Starts everything whose time comes before the horizon, appending a play for each clip, in the order they start
        void Schedule(std::uint64_t horizon, SoundEventMemory& memory, PlayList& plays);

        // Whether nothing is left to start or to end, every container having ended by the frame GetEnd gives, which is c_Never until then
        [[nodiscard]] bool IsFinished() const;
        [[nodiscard]] std::uint64_t GetEnd() const { return m_End; }
        [[nodiscard]] const SoundEventPlan& GetPlan() const { return *m_Plan; }

    private:
        static constexpr std::uint32_t c_NoActivation = std::numeric_limits<std::uint32_t>::max();

        // A container started, until it ends
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
        };

        enum class ActionKind : std::uint8_t
        {
            Begin,
            ChildEnded,
            Trigger
        };

        // Ordered by its time, then by the order it was made in, so ties go the same way on every run
        struct Action
        {
            std::uint64_t Time = 0;
            std::uint64_t Order = 0;
            std::uint32_t Activation = 0;
            ActionKind Kind = ActionKind::Begin;
        };

        void Activate(std::uint32_t node, std::uint32_t parent, std::uint64_t time);
        void Begin(std::uint32_t activation, std::uint64_t time);
        void Trigger(std::uint32_t activation, std::uint64_t time);
        void ChildEnded(std::uint32_t activation, std::uint64_t time);
        void End(std::uint32_t activation, std::uint64_t time);
        void Push(std::uint64_t time, std::uint32_t activation, ActionKind kind);
        [[nodiscard]] std::uint32_t Pick(const Activation& activation);
        [[nodiscard]] std::uint64_t DrawInterval(const SoundContainer& container);
        [[nodiscard]] std::uint64_t ToFrames(float seconds) const;

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
        // Set for the length of a Schedule
        SoundEventMemory* m_Memory = nullptr;
        PlayList* m_Plays = nullptr;
    };

    // A play of a clip as an instance's mix reads it: the main thread starts its voice, hands it to the mix and takes it back once the mix is done with it, and the rest is the audio thread's meanwhile
    struct SoundEventMixPlay
    {
        AudioClipVoice Voice;
        std::uint64_t Start = 0;
        std::uint64_t Length = 0;
        double Rate = 1.0;
        float Gain = 1.0f;
        std::uint32_t Channels = 1;
        // The clip's, which a play that loops comes back to the start at
        std::uint64_t Frames = 0;
        bool Loop = false;
        std::vector<float, TaggedAllocator<float, MemoryTag::Audio>> Buffer;
        std::atomic<bool> Done{ false };

        // The audio thread's: the instance's frames it has played, and the clip's frames from the play's start that its buffer holds
        bool Begun = false;
        std::uint64_t Played = 0;
        std::uint64_t BufferStart = 0;
        std::uint64_t Buffered = 0;
        bool InputEnded = false;
    };

    // An instance's plays mixed together, as a data source of miniaudio's that a sound reads as it would a clip, so the instance is one voice, heard from where its source is. Each play starts on the frame of the instance's clock it was given, its clip read at its rate between samples, so the plays of a Layer start on one sample and a Sequence's follow each other with no gap. A play given to it after its frame has passed starts where it would be by now. It reads silence while it waits for its next play, and ends once it has been told no more are coming and its last has played. The main thread hands plays over through a queue, and the audio thread never allocates, locks or waits
    class SoundEventMix
    {
    public:
        static constexpr std::uint32_t c_MaxPlays = 64;
        // The clip's frames a play reads ahead
        static constexpr std::uint32_t c_BufferFrames = 512;

        SoundEventMix(std::uint32_t channels, std::uint32_t sampleRate, std::uint64_t clock);
        ~SoundEventMix();

        SoundEventMix(const SoundEventMix&) = delete;
        SoundEventMix& operator=(const SoundEventMix&) = delete;

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
    };
}