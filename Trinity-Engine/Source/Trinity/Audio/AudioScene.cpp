#include "Trinity/Audio/AudioScene.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Audio/AudioClipAsset.hpp"
#include "Trinity/Audio/AudioClipStorage.hpp"
#include "Trinity/Audio/AudioEngine.hpp"
#include "Trinity/Audio/AudioEngineInternal.hpp"
#include "Trinity/Audio/SoundEventData.hpp"
#include "Trinity/Audio/SoundEventPlayer.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Physics/PhysicsScene2D.hpp"
#include "Trinity/Physics/PhysicsScene3D.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <miniaudio.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        // A pitch at or below 0 would stop the resampler, and Unity's sources go up to 3
        constexpr float c_MinimumPitch = 0.01f;
        constexpr float c_MaximumPitch = 3.0f;
        constexpr float c_MaximumDopplerLevel = 5.0f;
        constexpr float c_MaximumSpread = 180.0f;
        // miniaudio's exponential model as (min / d) to this power
        constexpr float c_ExponentialRolloff = 2.0f;
        constexpr float c_Pi = 3.14159265358979f;
        // The occlusion filter runs on this many channels at most, which 7.1 needs, and any further channel takes its volume alone
        constexpr std::size_t c_MaxOcclusionChannels = 8;
        // Butterworth's, so the filter is flat below its cutoff, 3 dB down at it and 12 dB an octave down above it
        constexpr float c_ButterworthQ = 0.70710678f;
        // A cutoff is kept between these, the top a little under half the sample rate, where the filter stays stable
        constexpr float c_MinimumCutoff = 10.0f;
        constexpr float c_MaximumCutoffRatio = 0.45f;
        // A source closer than this to its listener is never occluded, since a ray needs a direction
        constexpr float c_MinimumRay = 1e-4f;
        // A sound event's clips are scheduled this far ahead of where it plays, beyond two frames, and twice as far for one sped up, so a play reaches the audio thread before its frame, and a Switch picks by its parameter as it is about this long before
        constexpr float c_EventLookahead = 0.05f;
        // A sound event's frames at an engine without a rate, as a virtual voice's time is kept without an engine
        constexpr std::uint32_t c_DefaultSampleRate = 48000;
        // An instance a sound event's limit stops for a new one fades out over this long, as the mixer's ramps take, so stealing never clicks
        constexpr float c_StealFadeSeconds = 0.02f;
        // A real voice remembers where this many of its latest plays ended, for one handed over after them that starts once one has
        constexpr std::size_t c_EndsKept = 16;

        // Handles from one count for the life of the process, so one from a scene that has gone never reaches a voice of another
        std::atomic<std::uint64_t> s_NextHandle{ 1 };

        float ToRadians(float degrees)
        {
            return degrees * c_Pi / 180.0f;
        }

        ma_attenuation_model ToMiniaudio(AudioRolloff rolloff)
        {
            switch (rolloff)
            {
                case AudioRolloff::Linear:
                {
                    return ma_attenuation_model_linear;
                }
                case AudioRolloff::Exponential:
                {
                    return ma_attenuation_model_exponential;
                }
                case AudioRolloff::Logarithmic:
                {
                    break;
                }
            }

            return ma_attenuation_model_inverse;
        }

        // A maximum below the minimum would leave miniaudio's models attenuating nothing
        float GetMaxDistance(const AudioSourceComponent& source)
        {
            return std::max(source.MaxDistance, std::max(source.MinDistance, 0.0f));
        }

        // As miniaudio's cone has it, from the source's facing and the direction from it to the listener
        float GetConeGain(const AudioSourceComponent& source, const glm::vec3& forward, const glm::vec3& toListener)
        {
            const float l_Inner = ToRadians(source.ConeInnerAngle);
            if (l_Inner >= 2.0f * c_Pi)
            {
                return 1.0f;
            }

            const float l_CutoffInner = std::cos(l_Inner * 0.5f);
            const float l_CutoffOuter = std::cos(ToRadians(source.ConeOuterAngle) * 0.5f);
            const float l_Dot = glm::dot(forward, toListener);
            if (l_Dot > l_CutoffInner)
            {
                return 1.0f;
            }

            if (l_Dot > l_CutoffOuter)
            {
                return source.ConeOuterVolume + (1.0f - source.ConeOuterVolume) * (l_Dot - l_CutoffOuter) / (l_CutoffInner - l_CutoffOuter);
            }

            return source.ConeOuterVolume;
        }

        // Between a real voice's sound and its mixer group once its source asks to be occluded: the sound scaled by a volume, with a low-pass of it mixed in, both moved evenly to each of the main thread's values over a frame's worth of samples, so a change eased over many frames is one straight line and nothing steps. Clear, it is the sound exactly. The filter is a second-order Butterworth at the source's cutoff, and runs whether it is mixed in or not, so it never starts cold
        struct OcclusionNode
        {
            ma_node_base Base{};
            // Set by the main thread each frame and read by the audio thread each block
            std::atomic<float> TargetGain{ 1.0f };
            std::atomic<float> TargetWet{ 0.0f };
            std::atomic<float> TargetCutoff{ 0.0f };
            std::atomic<std::uint32_t> RampFrames{ 1 };
            // The audio thread's alone once the node is in the graph: where the volume and the filter's share are, the values they move to and the samples left to get there
            float Gain = 1.0f;
            float Wet = 0.0f;
            float RampGain = 1.0f;
            float RampWet = 0.0f;
            std::uint32_t RampLeft = 0;
            float Cutoff = 0.0f;
            float SampleRate = 0.0f;
            std::uint32_t Channels = 0;
            // The filter's coefficients, normalized by a0, and each channel's two registers, in transposed direct form II
            float B0 = 1.0f;
            float B1 = 0.0f;
            float B2 = 0.0f;
            float A1 = 0.0f;
            float A2 = 0.0f;
            std::array<std::array<float, 2>, c_MaxOcclusionChannels> Registers{};
        };

        static_assert(std::is_standard_layout_v<OcclusionNode>, "miniaudio reaches the node through its base, its first member.");

        // The cookbook's low-pass, as Robert Bristow-Johnson gives it
        void SetOcclusionCutoff(OcclusionNode& node, float cutoff)
        {
            node.Cutoff = cutoff;
            const float l_Cutoff = std::clamp(cutoff, c_MinimumCutoff, node.SampleRate * c_MaximumCutoffRatio);
            const float l_Omega = 2.0f * c_Pi * l_Cutoff / node.SampleRate;
            const float l_Cos = std::cos(l_Omega);
            const float l_Alpha = std::sin(l_Omega) / (2.0f * c_ButterworthQ);
            const float l_A0 = 1.0f + l_Alpha;
            node.B0 = (1.0f - l_Cos) * 0.5f / l_A0;
            node.B1 = (1.0f - l_Cos) / l_A0;
            node.B2 = node.B0;
            node.A1 = -2.0f * l_Cos / l_A0;
            node.A2 = (1.0f - l_Alpha) / l_A0;
        }

        // On the audio thread. A new value starts a ramp from where the volume and the filter's share are, each sample taking an even step of what is left, which ends exactly on it
        void ProcessOcclusion(ma_node* node, const float** framesIn, [[maybe_unused]] ma_uint32* frameCountIn, float** framesOut, ma_uint32* frameCountOut)
        {
            OcclusionNode& l_Node = *reinterpret_cast<OcclusionNode*>(node);
            const float l_Cutoff = l_Node.TargetCutoff.load(std::memory_order_relaxed);
            if (l_Cutoff != l_Node.Cutoff)
            {
                SetOcclusionCutoff(l_Node, l_Cutoff);
            }

            const float l_GainTarget = l_Node.TargetGain.load(std::memory_order_relaxed);
            const float l_WetTarget = l_Node.TargetWet.load(std::memory_order_relaxed);
            if (l_GainTarget != l_Node.RampGain || l_WetTarget != l_Node.RampWet)
            {
                l_Node.RampGain = l_GainTarget;
                l_Node.RampWet = l_WetTarget;
                l_Node.RampLeft = std::max(l_Node.RampFrames.load(std::memory_order_relaxed), 1u);
            }

            const ma_uint32 l_Frames = *frameCountOut;
            const std::uint32_t l_Channels = l_Node.Channels;
            const std::uint32_t l_Filtered = std::min<std::uint32_t>(l_Channels, c_MaxOcclusionChannels);
            const float* l_In = framesIn[0];
            float* l_Out = framesOut[0];
            for (ma_uint32 it_Frame = 0; it_Frame < l_Frames; ++it_Frame)
            {
                if (l_Node.RampLeft > 0)
                {
                    const float l_Left = static_cast<float>(l_Node.RampLeft);
                    l_Node.Gain += (l_Node.RampGain - l_Node.Gain) / l_Left;
                    l_Node.Wet += (l_Node.RampWet - l_Node.Wet) / l_Left;
                    if (--l_Node.RampLeft == 0)
                    {
                        l_Node.Gain = l_Node.RampGain;
                        l_Node.Wet = l_Node.RampWet;
                    }
                }

                const float l_Gain = l_Node.Gain;
                const float l_Wet = l_Node.Wet;
                const std::size_t l_Base = static_cast<std::size_t>(it_Frame) * l_Channels;
                for (std::uint32_t it_Channel = 0; it_Channel < l_Filtered; ++it_Channel)
                {
                    std::array<float, 2>& l_Registers = l_Node.Registers[it_Channel];
                    const float l_Dry = l_In[l_Base + it_Channel];
                    const float l_Low = l_Node.B0 * l_Dry + l_Registers[0];
                    l_Registers[0] = l_Node.B1 * l_Dry - l_Node.A1 * l_Low + l_Registers[1];
                    l_Registers[1] = l_Node.B2 * l_Dry - l_Node.A2 * l_Low;
                    l_Out[l_Base + it_Channel] = (l_Dry + l_Wet * (l_Low - l_Dry)) * l_Gain;
                }

                for (std::uint32_t it_Channel = l_Filtered; it_Channel < l_Channels; ++it_Channel)
                {
                    l_Out[l_Base + it_Channel] = l_In[l_Base + it_Channel] * l_Gain;
                }
            }
        }

        constexpr ma_node_vtable c_OcclusionVTable{ &ProcessOcclusion, nullptr, 1, 1, 0 };

        // An asset held while a voice plays it, whichever type it turns out to be once it loads
        class AssetHold
        {
        public:
            AssetHold() = default;

            explicit AssetHold(UUID id) : m_ID(id)
            {
                AssetManager::Acquire(m_ID);
            }

            ~AssetHold()
            {
                AssetManager::Release(m_ID);
            }

            AssetHold(AssetHold&& other) noexcept : m_ID(std::exchange(other.m_ID, UUID()))
            {

            }

            AssetHold& operator=(AssetHold&& other) noexcept
            {
                if (this != &other)
                {
                    AssetManager::Release(m_ID);
                    m_ID = std::exchange(other.m_ID, UUID());
                }

                return *this;
            }

            AssetHold(const AssetHold&) = delete;
            AssetHold& operator=(const AssetHold&) = delete;

            [[nodiscard]] UUID GetID() const { return m_ID; }

        private:
            UUID m_ID;
        };

        using MixPlayList = std::vector<SoundEventMixPlay*, TaggedAllocator<SoundEventMixPlay*, MemoryTag::Audio>>;
        using HoldList = std::vector<AssetHold, TaggedAllocator<AssetHold, MemoryTag::Audio>>;
        using StorageList = std::vector<Ref<AudioClipStorage>, TaggedAllocator<Ref<AudioClipStorage>, MemoryTag::Audio>>;
        using ClipTimeList = std::vector<SoundEventClip, TaggedAllocator<SoundEventClip, MemoryTag::Audio>>;
        using TargetList = std::vector<SoundParameterTarget, TaggedAllocator<SoundParameterTarget, MemoryTag::Audio>>;
        using FloatList = std::vector<float, TaggedAllocator<float, MemoryTag::Audio>>;

        struct EndedPlay
        {
            std::uint32_t Number = SoundEventPlayer::c_NoPlay;
            std::uint64_t Frame = 0;
        };

        // A source's own voice, one it plays beside it, which takes its values and its place, one played at a point, and one following an entity, which take their values from their resource
        enum class VoiceKind : std::uint8_t
        {
            Source,
            OneShot,
            Point,
            Attached
        };

        // The part of a voice that is mixed: its clip read through a voice of its own, or a sound event's plays through its mix, the sound miniaudio mixes that in, once its source asks to be occluded the node after the sound, and which of the two plays into the mixer's group the source names. Only a real voice has one, since a virtual voice needs only its time. A mix owns the plays handed to it, which it gives back once done, and those waiting for room in its queue
        struct RealVoice
        {
            AudioClipVoice Source;
            SoundEventMix* Mix = nullptr;
            MixPlayList Plays;
            MixPlayList Waiting;
            ma_sound Sound{};
            bool HasSound = false;
            OcclusionNode Occlusion;
            bool HasOcclusion = false;
            ma_node* Routed = nullptr;
            UUID RoutedGroup;
        };

        // One playing sound: its kind, its handle, the entity it plays from, a source's or one it follows, its resource, held while it plays, where it is in it, and the values it plays with as last read. A sound event's has its plan, the clips it holds, each as the schedule times it, the schedule, the plays scheduled that have not yet ended, the channels its mix has, the seed of its choices and the memory it keeps them in
        struct Voice
        {
            VoiceKind Kind = VoiceKind::Source;
            std::uint64_t Handle = 0;
            UUID Entity;
            UUID Resource;
            AssetHold Held;
            Ref<AudioClipStorage> Storage;
            RealVoice* Real = nullptr;
            AudioSourceComponent Values;
            glm::vec3 Position{ 0.0f };
            glm::vec3 Forward{ 0.0f, 0.0f, -1.0f };
            glm::vec3 Velocity{ 0.0f };
            // In the clip's frames, or a sound event's at the engine's rate
            double Cursor = 0.0;
            Ref<const SoundEventPlan> Plan;
            HoldList EventClips;
            StorageList ClipStorages;
            ClipTimeList ClipTimes;
            // A version of its event loaded since it began, and the clips it names, held while they load, which it moves to once they have
            Ref<const SoundEventPlan> NextPlan;
            HoldList NextClips;
            std::optional<SoundEventPlayer> Player;
            SoundEventPlayer::PlayList Scheduled;
            std::uint32_t Channels = 1;
            std::uint64_t Seed = 0;
            Ref<SoundEventMemory> Memory;
            // A sound event's parameters, in the event's order: its own targets, the target each reads, its own or the engine's global one, the latest value each was given, which a Switch picks by, and where each is, read from its mix while it is real and sought here while it is not, as of the engine's frame given, with its containers' values and pitches from them
            TargetList Targets;
            std::vector<const SoundParameterTarget*, TaggedAllocator<const SoundParameterTarget*, MemoryTag::Audio>> Reads;
            FloatList Latest;
            FloatList Heard;
            std::uint64_t HeardFrame = 0;
            std::vector<SoundEventPlan::NodeValue, TaggedAllocator<SoundEventPlan::NodeValue, MemoryTag::Audio>> Nodes;
            FloatList Pitches;
            // Where its latest plays ended, for one handed over after them that starts once one has
            std::array<EndedPlay, c_EndsKept> Ends{};
            std::size_t NextEnd = 0;
            // A one-shot's volume against its source's, or the volume one played on its own was given
            float Scale = 1.0f;
            // Whether one following an entity has a place yet, which its velocity is taken from
            bool Placed = false;
            // Fading out over the seconds left, no longer counted against its event's limit
            bool Stopping = false;
            float StopLeft = 0.0f;
            // The order voices began in, which settles a tie between two of the same priority and loudness
            std::uint64_t Number = 0;
            float Loudness = 0.0f;
            bool Loaded = false;
            bool Paused = false;
            bool Ended = false;
            // How muffled it is, from 0 to 1, eased towards whether its last ray was stopped, and that ray, from the listener to the source as far as what stopped it, cast in the frame given, none before 0
            float Occlusion = 0.0f;
            bool Blocked = false;
            std::uint64_t OcclusionFrame = 0;
            glm::vec3 RayFrom{ 0.0f };
            glm::vec3 RayTo{ 0.0f };
            glm::vec3 RayHit{ 0.0f };
        };

        // A source is occluded only from where it is heard, so only a spatialized one
        bool IsOccluded(const AudioSourceComponent& source)
        {
            return source.Occlusion && source.Spatialize;
        }

        float GetOcclusionGain(const AudioSourceComponent& source, float amount)
        {
            return 1.0f + (std::clamp(source.OcclusionVolume, 0.0f, 1.0f) - 1.0f) * amount;
        }

        // The entity whose body the entity's colliders are part of, for a ray to pass over: the nearest above it, or itself, with a RigidBody or a CharacterController of the world asked for, or itself without one
        template<typename Body, typename Character>
        UUID FindBodyEntity(Entity entity)
        {
            for (Entity it_Entity = entity; it_Entity; it_Entity = it_Entity.GetParent())
            {
                if (it_Entity.Has<Body>() || it_Entity.Has<Character>())
                {
                    return it_Entity.GetUUID();
                }
            }

            return entity ? entity.GetUUID() : UUID();
        }

        // What a ray to a source meets of the source itself does not occlude it: a collider of its entity or of one below it, or its body's
        bool IsSourceHit(Scene& scene, UUID source, UUID sourceBody, UUID hitEntity, UUID hitBody)
        {
            if (hitBody == sourceBody)
            {
                return true;
            }

            for (Entity it_Entity = scene.FindEntityByUUID(hitEntity); it_Entity; it_Entity = it_Entity.GetParent())
            {
                if (it_Entity.GetUUID() == source)
                {
                    return true;
                }
            }

            return false;
        }

        // A source the scene has had, seen each frame it still has it, with its values, where it is, which way it faces and how fast it moves, as its voices take them, whether it has been woken, which plays it if it Plays On Awake, unless it has played already, and what it keeps of the sound events it plays from one play to the next
        struct SourceState
        {
            Voice* Playing = nullptr;
            std::uint64_t Seen = 0;
            AudioSourceComponent Values;
            glm::vec3 Position{ 0.0f };
            glm::vec3 Forward{ 0.0f, 0.0f, -1.0f };
            glm::vec3 Velocity{ 0.0f };
            bool Placed = false;
            bool Awake = false;
            Ref<SoundEventMemory> Memory;
        };

        // Facing its -Z, as a source and a listener do
        glm::vec3 GetForward(const glm::mat4& world)
        {
            const glm::vec3 l_Facing(world[2]);

            return glm::dot(l_Facing, l_Facing) > 0.0f ? -glm::normalize(l_Facing) : glm::vec3(0.0f, 0.0f, -1.0f);
        }

        // A sound event played on its own, as its own source would play it, at the volume it was given
        AudioSourceComponent MakeEventValues(const SoundEventData& data, UUID resource, float volume)
        {
            AudioSourceComponent l_Values;
            l_Values.Resource = resource;
            l_Values.Output = data.Output;
            l_Values.PlayOnAwake = false;
            l_Values.Priority = data.Priority;
            l_Values.Volume = volume;
            l_Values.Spatialize = data.Spatialize;
            l_Values.DopplerLevel = data.DopplerLevel;
            l_Values.Spread = data.Spread;
            l_Values.MinDistance = data.MinDistance;
            l_Values.MaxDistance = data.MaxDistance;
            l_Values.Rolloff = data.Rolloff;
            l_Values.Occlusion = data.Occlusion;
            l_Values.OcclusionVolume = data.OcclusionVolume;
            l_Values.OcclusionCutoff = data.OcclusionCutoff;

            return l_Values;
        }

        Ref<SoundEventMemory> MakeMemory()
        {
            return std::allocate_shared<SoundEventMemory>(TaggedAllocator<SoundEventMemory, MemoryTag::Audio>());
        }

        // A clip's frames as it plays them: a clip decoded at load holds what its decode gave
        std::uint64_t CountFrames(const AudioClipStorage& storage)
        {
            const AudioClipHeader& l_Header = storage.Header;
            if (l_Header.LoadType != AudioLoadType::DecompressOnLoad)
            {
                return l_Header.Frames;
            }

            const std::uint64_t l_FrameSize = std::uint64_t{ l_Header.Channels } * GetSampleSize(l_Header.Format);

            return l_FrameSize != 0 ? storage.Samples.size() / l_FrameSize : 0;
        }


        // Each clip held as the schedule times it, once each has loaded its samples or failed, which plays as nothing, as does a clip that is no clip, and the channels of the widest. False while one still loads
        bool ReadEventClips(std::span<const AssetHold> holds, StorageList& storages, ClipTimeList& times, std::uint32_t& channels)
        {
            storages.clear();
            times.clear();
            channels = 1;
            for (const AssetHold& it_Clip : holds)
            {
                const AssetState l_State = AssetManager::GetState(it_Clip.GetID());
                const Asset* l_Asset = l_State == AssetState::Ready ? AssetManager::GetAsset(it_Clip.GetID()) : nullptr;
                const Ref<AudioClipStorage>* l_Storage = l_Asset != nullptr && l_Asset->GetAssetType() == AudioClipAsset::c_AssetType ? &static_cast<const AudioClipAsset*>(l_Asset)->GetStorage() : nullptr;
                const AudioClipStatus l_Status = l_Storage != nullptr ? (*l_Storage)->Status.load(std::memory_order_acquire) : AudioClipStatus::Failed;
                if (l_State == AssetState::Loading || l_Status == AudioClipStatus::Loading)
                {
                    return false;
                }

                const bool l_Plays = l_Status == AudioClipStatus::Loaded && CountFrames(**l_Storage) != 0;
                storages.push_back(l_Plays ? *l_Storage : Ref<AudioClipStorage>());
                times.push_back(l_Plays ? SoundEventClip{ CountFrames(**l_Storage), (*l_Storage)->Header.SampleRate } : SoundEventClip());
                channels = std::max(channels, l_Plays ? (*l_Storage)->Header.Channels : 1u);
            }

            return true;
        }

        // Its clip's voice started at once, a stream refilled before it plays, as a clip's own voice is, with what its container's curves move
        SoundEventMixPlay* MakeMixPlay(AudioEngine& engine, const Ref<AudioClipStorage>& storage, const SoundEventPlayer::Play& play, const SoundEventPlan& plan)
        {
            SoundEventMixPlay* l_Play = Memory::New<SoundEventMixPlay>(MemoryTag::Audio);
            if (!l_Play->Voice.Start(storage))
            {
                Memory::Delete(l_Play);

                return nullptr;
            }

            l_Play->Start = play.Start;
            l_Play->Length = play.Length;
            l_Play->Rate = play.Rate;
            l_Play->Gain = play.Gain;
            l_Play->Loop = play.Loop;
            l_Play->Pitch = play.Pitch;
            l_Play->Node = play.Node;
            l_Play->Number = play.Number;
            l_Play->Offset = play.Offset;
            if (play.Node < plan.Nodes.size())
            {
                l_Play->Faded = plan.Nodes[play.Node].Faded;
                l_Play->Pitched = plan.Nodes[play.Node].Pitched;
                l_Play->Filtered = plan.Nodes[play.Node].Filtered;
            }

            l_Play->Channels = storage->Header.Channels;
            l_Play->Frames = CountFrames(*storage);
            l_Play->Buffer.resize(std::size_t{ SoundEventMix::c_BufferFrames } * l_Play->Channels);
            static_cast<void>(ma_data_source_set_looping(l_Play->Voice.GetDataSource(), play.Loop ? MA_TRUE : MA_FALSE));
            if (AudioStream* l_Stream = l_Play->Voice.GetStream())
            {
                AudioEngineInternal::AddStream(engine, *l_Stream);
                if (engine.GetOutput() == AudioOutput::Offline)
                {
                    l_Stream->RefillNow();
                }
                else
                {
                    l_Stream->RequestRefill();
                }
            }

            return l_Play;
        }

        void DestroyMixPlay(AudioEngine* engine, SoundEventMixPlay* play)
        {
            if (AudioStream* l_Stream = play->Voice.GetStream(); l_Stream != nullptr && engine != nullptr)
            {
                AudioEngineInternal::RemoveStream(*engine, *l_Stream);
            }

            Memory::Delete(play);
        }

        using SourceMap = std::unordered_map<UUID, SourceState, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, SourceState>, MemoryTag::Audio>>;
        using VoiceList = std::vector<Voice*, TaggedAllocator<Voice*, MemoryTag::Audio>>;

        // The sound goes before the voice it reads, and a stream leaves the engine's refills before it goes
        void DestroyReal(AudioEngine* engine, Voice& voice)
        {
            RealVoice* l_Real = std::exchange(voice.Real, nullptr);
            if (l_Real == nullptr)
            {
                return;
            }

            if (l_Real->Routed != nullptr && engine != nullptr)
            {
                AudioEngineInternal::Unroute(*engine, l_Real->Routed);
            }

            if (l_Real->HasSound)
            {
                ma_uint64 l_Cursor = 0;
                if (ma_sound_get_cursor_in_pcm_frames(&l_Real->Sound, &l_Cursor) == MA_SUCCESS)
                {
                    voice.Cursor = static_cast<double>(l_Cursor);
                }

                ma_sound_uninit(&l_Real->Sound);
            }

            // A sound event's parameters go on from where its mix had them, sought from here on the main thread
            if (l_Real->Mix != nullptr && engine != nullptr)
            {
                for (std::size_t it_Parameter = 0; it_Parameter < voice.Heard.size(); ++it_Parameter)
                {
                    voice.Heard[it_Parameter] = l_Real->Mix->GetValue(it_Parameter);
                }

                voice.HeardFrame = ma_engine_get_time_in_pcm_frames(&AudioEngineInternal::GetEngine(*engine));
            }

            if (l_Real->HasOcclusion)
            {
                ma_node_uninit(&l_Real->Occlusion.Base, &AudioEngineInternal::GetEngine(*engine).allocationCallbacks);
            }

            if (AudioStream* l_Stream = l_Real->Source.GetStream(); l_Stream != nullptr && engine != nullptr)
            {
                AudioEngineInternal::RemoveStream(*engine, *l_Stream);
            }

            // Once the sound is gone, nothing reads the mix or its plays
            for (SoundEventMixPlay* it_Play : l_Real->Plays)
            {
                DestroyMixPlay(engine, it_Play);
            }

            for (SoundEventMixPlay* it_Play : l_Real->Waiting)
            {
                DestroyMixPlay(engine, it_Play);
            }

            Memory::Delete(l_Real->Mix);
            l_Real->Source.Stop();
            Memory::Delete(l_Real);
        }

        void DestroyVoice(AudioEngine* engine, Voice* voice)
        {
            DestroyReal(engine, *voice);
            Memory::Delete(voice);
        }

        // The source's values on its sound, each frame it is real
        void ApplyValues(Voice& voice, const AudioSettings& settings)
        {
            ma_sound& l_Sound = voice.Real->Sound;
            const AudioSourceComponent& l_Values = voice.Values;
            ma_sound_set_volume(&l_Sound, l_Values.Mute ? 0.0f : std::clamp(l_Values.Volume, 0.0f, 1.0f));
            ma_sound_set_pitch(&l_Sound, std::clamp(l_Values.Pitch, c_MinimumPitch, c_MaximumPitch));
            ma_sound_set_pan(&l_Sound, std::clamp(l_Values.StereoPan, -1.0f, 1.0f));
            // A sound event loops as its containers say
            ma_sound_set_looping(&l_Sound, l_Values.Loop && voice.Plan == nullptr ? MA_TRUE : MA_FALSE);
            ma_sound_set_spatialization_enabled(&l_Sound, l_Values.Spatialize ? MA_TRUE : MA_FALSE);
            if (!l_Values.Spatialize)
            {
                return;
            }

            ma_sound_set_position(&l_Sound, voice.Position.x, voice.Position.y, voice.Position.z);
            ma_sound_set_direction(&l_Sound, voice.Forward.x, voice.Forward.y, voice.Forward.z);
            ma_sound_set_velocity(&l_Sound, voice.Velocity.x, voice.Velocity.y, voice.Velocity.z);
            ma_sound_set_attenuation_model(&l_Sound, ToMiniaudio(l_Values.Rolloff));
            ma_sound_set_rolloff(&l_Sound, l_Values.Rolloff == AudioRolloff::Exponential ? c_ExponentialRolloff : 1.0f);
            ma_sound_set_min_distance(&l_Sound, std::max(l_Values.MinDistance, 0.0f));
            ma_sound_set_max_distance(&l_Sound, GetMaxDistance(l_Values));
            ma_sound_set_cone(&l_Sound, ToRadians(l_Values.ConeInnerAngle), ToRadians(l_Values.ConeOuterAngle), std::clamp(l_Values.ConeOuterVolume, 0.0f, 1.0f));
            ma_sound_set_doppler_factor(&l_Sound, std::clamp(l_Values.DopplerLevel, 0.0f, c_MaximumDopplerLevel) * std::max(settings.DopplerFactor, 0.0f));
            ma_sound_set_directional_attenuation_factor(&l_Sound, 1.0f - std::clamp(l_Values.Spread, 0.0f, c_MaximumSpread) / c_MaximumSpread);
        }

        // The values its occlusion node moves to over the samples of a frame as long as this one, before the next frame's
        void ApplyOcclusion(Voice& voice, float seconds)
        {
            OcclusionNode& l_Node = voice.Real->Occlusion;
            l_Node.RampFrames.store(static_cast<std::uint32_t>(std::max(seconds * l_Node.SampleRate, 1.0f)), std::memory_order_relaxed);
            l_Node.TargetGain.store(GetOcclusionGain(voice.Values, voice.Occlusion), std::memory_order_relaxed);
            l_Node.TargetWet.store(voice.Occlusion, std::memory_order_relaxed);
            l_Node.TargetCutoff.store(voice.Values.OcclusionCutoff, std::memory_order_relaxed);
        }
    }

    // As miniaudio's models have it, so a voice's loudness is what it would sound at
    float GetAudioRolloffGain(const AudioSourceComponent& source, float distance)
    {
        const float l_Minimum = std::max(source.MinDistance, 0.0f);
        const float l_Maximum = GetMaxDistance(source);
        if (l_Minimum >= l_Maximum)
        {
            return 1.0f;
        }

        const float l_Distance = std::clamp(distance, l_Minimum, l_Maximum);
        switch (source.Rolloff)
        {
            case AudioRolloff::Linear:
            {
                return 1.0f - (l_Distance - l_Minimum) / (l_Maximum - l_Minimum);
            }
            case AudioRolloff::Exponential:
            {
                return l_Minimum > 0.0f ? std::pow(l_Distance / l_Minimum, -c_ExponentialRolloff) : 1.0f;
            }
            case AudioRolloff::Logarithmic:
            {
                break;
            }
        }

        return l_Minimum > 0.0f ? l_Minimum / l_Distance : 1.0f;
    }

    struct AudioCarry::State
    {
        AudioEngine* Engine = nullptr;
        std::vector<std::pair<UUID, SourceState>, TaggedAllocator<std::pair<UUID, SourceState>, MemoryTag::Audio>> Sources;
        VoiceList Voices;
    };

    // Voices never adopted stop with it
    AudioCarry::~AudioCarry()
    {
        if (m_State == nullptr)
        {
            return;
        }

        for (Voice* it_Voice : m_State->Voices)
        {
            DestroyVoice(m_State->Engine, it_Voice);
        }

        Memory::Delete(m_State);
    }

    AudioCarry::AudioCarry(AudioCarry&& other) noexcept : m_State(std::exchange(other.m_State, nullptr))
    {

    }

    AudioCarry& AudioCarry::operator=(AudioCarry&& other) noexcept
    {
        if (this != &other)
        {
            AudioCarry l_Old(std::move(*this));
            m_State = std::exchange(other.m_State, nullptr);
        }

        return *this;
    }

    struct AudioScene::State
    {
        State(Scene& scene, AudioEngine* engine, const AudioSettings& settings) : TheScene(scene), Engine(engine), Settings(settings)
        {

        }

        void UpdateListener(float seconds);
        void UpdateOcclusion(float seconds);
        void CastOcclusionRay(Voice& voice, UUID listenerBody);
        [[nodiscard]] SourceState& FindSource(Entity entity);
        Voice* NewVoice(VoiceKind kind, UUID entity, UUID resource, const AudioSourceComponent& values, const glm::vec3& position, Ref<SoundEventMemory> memory, std::uint64_t seed);
        void StartVoice(SourceState& source, UUID entity, const AudioSourceComponent& values);
        void EndVoice(SourceState& source);
        void RemoveVoice(Voice* voice);
        void StopVoice(Voice& voice, float seconds);
        [[nodiscard]] Voice* FindVoice(AudioHandle handle) const;
        [[nodiscard]] double GetTime(const Voice& voice) const;
        [[nodiscard]] AudioHandle PlayOwn(VoiceKind kind, UUID resource, Entity entity, const glm::vec3& position, float volume);
        void PlaceVoice(Voice& voice, float seconds);
        [[nodiscard]] float GetLoudness(const Voice& voice) const;
        void Load(Voice& voice);
        void LoadEvent(Voice& voice, const SoundEventAsset& asset);
        void PrepareParameters(Voice& voice, const SoundEventPlan* previous = nullptr);
        void Refresh(Voice& voice);
        void SeekParameters(Voice& voice);
        void GiveParameter(const SoundEventMemory& memory, std::string_view name, float value);
        [[nodiscard]] std::optional<float> GetHeardParameter(const Voice& voice, std::string_view name) const;
        void ResolveAnchor(Voice& voice, SoundEventMixPlay& play, const SoundEventPlayer::Play& scheduled) const;
        [[nodiscard]] std::uint64_t GetClockFrame() const;
        [[nodiscard]] std::uint64_t GetChangeFrame() const;
        [[nodiscard]] bool Admit(const Voice& voice);
        [[nodiscard]] std::uint64_t CountOwnPlay(UUID resource);
        void ChooseRealVoices();
        void MakeReal(Voice& voice);
        void StartSound(Voice& voice);
        void AttachOcclusion(Voice& voice, bool heard);
        void RouteVoice(Voice& voice);
        void Advance(Voice& voice, float seconds);
        void ScheduleEvent(Voice& voice);
        void RemoveEnded();
        [[nodiscard]] std::uint32_t GetSampleRate() const;
        [[nodiscard]] Ref<const SoundEventPlan> FindPlan(UUID event, const SoundEventAsset& asset);

        Scene& TheScene;
        AudioEngine* Engine = nullptr;
        AudioSettings Settings;
        SourceMap Sources;
        // Every voice playing, ordered each frame by how much it matters
        VoiceList Voices;
        std::uint64_t Frame = 0;
        float FrameSeconds = 0.0f;
        std::uint64_t NextNumber = 0;

        bool HasListener = false;
        bool ListenerPlaced = false;
        UUID Listener;
        glm::vec3 ListenerPosition{ 0.0f };
        std::size_t ListenerCount = 0;
        bool WarnedNoListener = false;
        bool WarnedVoices = false;

        const PhysicsScene3D* Physics = nullptr;
        const PhysicsScene2D* Physics2D = nullptr;
        // The voices waiting for a ray, kept so a frame allocates nothing once it has room
        VoiceList OcclusionQueue;
        std::uint32_t OcclusionRays = 0;

        // A plan for each version of each sound event playing, shared by its voices, and let go once none plays it
        std::vector<Ref<const SoundEventPlan>, TaggedAllocator<Ref<const SoundEventPlan>, MemoryTag::Audio>> Plans;
        bool WarnedPlays = false;

        // Seconds the scene has played, not paused, when each sound event last started an instance, for its cooldown, and how often each resource has been played on its own, which seeds those plays
        double Time = 0.0;
        std::vector<std::pair<UUID, double>, TaggedAllocator<std::pair<UUID, double>, MemoryTag::Audio>> LastStarts;
        std::vector<std::pair<UUID, std::uint64_t>, TaggedAllocator<std::pair<UUID, std::uint64_t>, MemoryTag::Audio>> OwnPlays;
        bool ScenePaused = false;
    };

    // The first listener in hierarchy order, which with one is the only one. A second, and none while sounds play, are each told once until it changes
    void AudioScene::State::UpdateListener(float seconds)
    {
        const auto l_Listeners = TheScene.GetRegistry().view<AudioListenerComponent>();
        const std::size_t l_Count = l_Listeners.size();
        Entity l_Chosen;
        if (l_Count == 1)
        {
            l_Chosen = Entity(l_Listeners.front(), &TheScene);
        }
        else if (l_Count > 1)
        {
            for (Entity it_Entity = TheScene.GetFirstRoot(); it_Entity; it_Entity = TheScene.GetNextInHierarchyOrder(it_Entity))
            {
                if (it_Entity.Has<AudioListenerComponent>())
                {
                    l_Chosen = it_Entity;
                    break;
                }
            }
        }

        if (l_Count > 1 && ListenerCount <= 1 && l_Chosen)
        {
            TR_CORE_WARN("Audio: the scene has {} AudioListeners, so it is heard only through {}'s, the first in hierarchy order", l_Count, std::string(l_Chosen.Get<TagComponent>().Tag));
        }

        ListenerCount = l_Count;
        HasListener = static_cast<bool>(l_Chosen);
        if (!HasListener)
        {
            ListenerPlaced = false;
            if (!WarnedNoListener && !Voices.empty())
            {
                TR_CORE_WARN("Audio: the scene has no AudioListener, so its sounds play in silence");
                WarnedNoListener = true;
            }

            return;
        }

        WarnedNoListener = false;
        const glm::mat4& l_World = l_Chosen.Get<WorldTransformComponent>().Matrix;
        const glm::vec3 l_Position(l_World[3]);
        const glm::vec3 l_Forward = -glm::normalize(glm::vec3(l_World[2]));
        const glm::vec3 l_Up = glm::normalize(glm::vec3(l_World[1]));
        const bool l_Moved = ListenerPlaced && Listener == l_Chosen.GetUUID() && seconds > 0.0f;
        const glm::vec3 l_Velocity = l_Moved ? (l_Position - ListenerPosition) / seconds : glm::vec3(0.0f);
        Listener = l_Chosen.GetUUID();
        ListenerPosition = l_Position;
        ListenerPlaced = true;

        if (Engine != nullptr)
        {
            ma_engine& l_Engine = AudioEngineInternal::GetEngine(*Engine);
            ma_engine_listener_set_position(&l_Engine, 0, l_Position.x, l_Position.y, l_Position.z);
            ma_engine_listener_set_direction(&l_Engine, 0, l_Forward.x, l_Forward.y, l_Forward.z);
            ma_engine_listener_set_world_up(&l_Engine, 0, l_Up.x, l_Up.y, l_Up.z);
            ma_engine_listener_set_velocity(&l_Engine, 0, l_Velocity.x, l_Velocity.y, l_Velocity.z);
        }
    }

    // Each voice that asks to be occluded, the longest unchecked first and one never checked before them all, gets a ray from the listener while the project's rays a frame last, which passes over the listener's own body. Then every voice eases towards what its last ray found, and one no longer occluded eases back to clear. With no world, or no rays a frame, nothing is in the way, and with no listener nothing is heard, so neither casts
    void AudioScene::State::UpdateOcclusion(float seconds)
    {
        OcclusionRays = 0;
        if (!HasListener)
        {
            return;
        }

        OcclusionQueue.clear();
        for (Voice* it_Voice : Voices)
        {
            if (!IsOccluded(it_Voice->Values))
            {
                it_Voice->Blocked = false;
            }
            else if (!it_Voice->Paused && !it_Voice->Ended)
            {
                OcclusionQueue.push_back(it_Voice);
            }
        }

        if ((Physics == nullptr && Physics2D == nullptr) || Settings.OcclusionRays == 0)
        {
            for (Voice* it_Voice : OcclusionQueue)
            {
                it_Voice->Blocked = false;
                it_Voice->OcclusionFrame = Frame;
                it_Voice->RayFrom = ListenerPosition;
                it_Voice->RayTo = it_Voice->Position;
                it_Voice->RayHit = it_Voice->Position;
            }
        }
        else
        {
            const std::size_t l_Rays = std::min<std::size_t>(Settings.OcclusionRays, OcclusionQueue.size());
            const auto a_Oldest = [](const Voice* left, const Voice* right) { return std::tie(left->OcclusionFrame, left->Number) < std::tie(right->OcclusionFrame, right->Number); };
            if (l_Rays < OcclusionQueue.size())
            {
                std::ranges::nth_element(OcclusionQueue, OcclusionQueue.begin() + static_cast<std::ptrdiff_t>(l_Rays), a_Oldest);
            }

            const Entity l_Listener = TheScene.FindEntityByUUID(Listener);
            const UUID l_ListenerBody = Physics != nullptr ? FindBodyEntity<RigidBodyComponent, CharacterControllerComponent>(l_Listener) : FindBodyEntity<RigidBody2DComponent, CharacterController2DComponent>(l_Listener);
            for (std::size_t it_Voice = 0; it_Voice < l_Rays; ++it_Voice)
            {
                CastOcclusionRay(*OcclusionQueue[it_Voice], l_ListenerBody);
            }
        }

        const float l_Step = Settings.OcclusionTime > 0.0f ? seconds / Settings.OcclusionTime : 1.0f;
        for (Voice* it_Voice : Voices)
        {
            const float l_Target = it_Voice->Blocked ? 1.0f : 0.0f;
            it_Voice->Occlusion = it_Voice->Occlusion < l_Target ? std::min(it_Voice->Occlusion + l_Step, l_Target) : std::max(it_Voice->Occlusion - l_Step, l_Target);
        }
    }

    // From the listener to the source, against the 3D world if the scene has one and otherwise the 2D one in its plane, on the occlusion layers, triggers left out. What the ray meets of the source itself does not count. The first ray of a voice not yet heard is taken at once, so a sound that starts behind a wall never begins clear, and one already heard, whose source has just asked to be occluded, eases into it
    void AudioScene::State::CastOcclusionRay(Voice& voice, UUID listenerBody)
    {
        ++OcclusionRays;
        const bool l_First = voice.OcclusionFrame == 0;
        voice.OcclusionFrame = Frame;
        voice.Blocked = false;
        voice.RayFrom = ListenerPosition;
        voice.RayTo = voice.Position;
        voice.RayHit = voice.Position;

        const Entity l_Source = TheScene.FindEntityByUUID(voice.Entity);
        if (Physics != nullptr)
        {
            const glm::vec3 l_Ray = voice.Position - ListenerPosition;
            const float l_Distance = glm::length(l_Ray);
            const PhysicsQueryFilter3D l_Filter{ Settings.OcclusionLayers, false, listenerBody };
            const std::optional<PhysicsHit3D> l_Hit = l_Distance > c_MinimumRay ? Physics->Raycast(ListenerPosition, l_Ray, l_Distance, l_Filter) : std::nullopt;
            if (l_Hit && !IsSourceHit(TheScene, voice.Entity, FindBodyEntity<RigidBodyComponent, CharacterControllerComponent>(l_Source), l_Hit->Entity, l_Hit->Body))
            {
                voice.Blocked = true;
                voice.RayHit = l_Hit->Point;
            }
        }
        else if (Physics2D != nullptr)
        {
            const glm::vec2 l_From(ListenerPosition);
            const glm::vec2 l_Ray = glm::vec2(voice.Position) - l_From;
            const float l_Distance = glm::length(l_Ray);
            const PhysicsQueryFilter2D l_Filter{ Settings.OcclusionLayers, false, listenerBody };
            const std::optional<PhysicsHit2D> l_Hit = l_Distance > c_MinimumRay ? Physics2D->Raycast(l_From, l_Ray, l_Distance, l_Filter) : std::nullopt;
            if (l_Hit && !IsSourceHit(TheScene, voice.Entity, FindBodyEntity<RigidBody2DComponent, CharacterController2DComponent>(l_Source), l_Hit->Entity, l_Hit->Body))
            {
                voice.Blocked = true;
                voice.RayHit = glm::vec3(l_Hit->Point, glm::mix(ListenerPosition.z, voice.Position.z, l_Hit->Distance / l_Distance));
            }
        }

        if (l_First && voice.Real == nullptr)
        {
            voice.Occlusion = voice.Blocked ? 1.0f : 0.0f;
        }
    }

    // Held from now on, so its resource loads if nothing else has it, and heard once it has. Each play is counted, which seeds a sound event's choices
    void AudioScene::State::StartVoice(SourceState& source, UUID entity, const AudioSourceComponent& values)
    {
        EndVoice(source);
        if (!values.Resource)
        {
            return;
        }

        if (!source.Memory)
        {
            source.Memory = MakeMemory();
        }

        Voice* l_Voice = NewVoice(VoiceKind::Source, entity, values.Resource, values, source.Position, source.Memory, SoundEventRandom::MakeSeed(values.Resource, entity, source.Memory->Plays++));
        l_Voice->Forward = source.Forward;
        source.Playing = l_Voice;
    }

    // A source a call reaches before the scene's next update has seen it, as it is now, its memory made for it. The entity has a source
    SourceState& AudioScene::State::FindSource(Entity entity)
    {
        const auto [a_Found, a_New] = Sources.try_emplace(entity.GetUUID());
        SourceState& l_Source = a_Found->second;
        if (a_New)
        {
            const glm::mat4& l_World = entity.Get<WorldTransformComponent>().Matrix;
            l_Source.Seen = Frame;
            l_Source.Position = glm::vec3(l_World[3]);
            l_Source.Forward = GetForward(l_World);
            l_Source.Values = entity.Get<AudioSourceComponent>();
        }

        if (!l_Source.Memory)
        {
            l_Source.Memory = MakeMemory();
        }

        return l_Source;
    }

    // With a handle of its own, its resource held from now on
    Voice* AudioScene::State::NewVoice(VoiceKind kind, UUID entity, UUID resource, const AudioSourceComponent& values, const glm::vec3& position, Ref<SoundEventMemory> memory, std::uint64_t seed)
    {
        Voice* l_Voice = Memory::New<Voice>(MemoryTag::Audio);
        l_Voice->Kind = kind;
        l_Voice->Handle = s_NextHandle.fetch_add(1, std::memory_order_relaxed);
        l_Voice->Entity = entity;
        l_Voice->Resource = resource;
        l_Voice->Held = AssetHold(resource);
        l_Voice->Memory = std::move(memory);
        l_Voice->Seed = seed;
        l_Voice->Values = values;
        l_Voice->Position = position;
        l_Voice->Placed = true;
        l_Voice->Number = NextNumber++;
        Voices.push_back(l_Voice);

        return l_Voice;
    }

    void AudioScene::State::RemoveVoice(Voice* voice)
    {
        if (const auto a_Source = Sources.find(voice->Entity); a_Source != Sources.end() && a_Source->second.Playing == voice)
        {
            a_Source->second.Playing = nullptr;
        }

        std::erase(Voices, voice);
        DestroyVoice(Engine, voice);
    }

    // A real voice's sound fades out on the audio thread and stops, and a virtual voice counts the time down. One that cannot be heard fading, paused or not yet sounding, ends at once
    void AudioScene::State::StopVoice(Voice& voice, float seconds)
    {
        if (voice.Ended || voice.Stopping)
        {
            return;
        }

        const bool l_Sounding = voice.Real != nullptr && voice.Real->HasSound;
        if (seconds <= 0.0f || voice.Paused || ScenePaused || (voice.Real != nullptr && !l_Sounding))
        {
            voice.Ended = true;

            return;
        }

        voice.Stopping = true;
        voice.StopLeft = seconds;
        if (l_Sounding)
        {
            static_cast<void>(ma_sound_stop_with_fade_in_pcm_frames(&voice.Real->Sound, std::max<ma_uint64>(static_cast<ma_uint64>(seconds * static_cast<float>(GetSampleRate())), 1)));
        }
    }

    Voice* AudioScene::State::FindVoice(AudioHandle handle) const
    {
        const auto a_Found = handle ? std::ranges::find_if(Voices, [handle](const Voice* voice) { return voice->Handle == handle.ID && !voice->Ended; }) : Voices.end();

        return a_Found != Voices.end() ? *a_Found : nullptr;
    }

    // A source's voice and its one-shots take the source's values and place, a one-shot's volume scaled and its loop left out, and end once the source is gone. One following an entity takes the entity's place until it is gone, and stays where it was then, and one at a point stays at it
    void AudioScene::State::PlaceVoice(Voice& voice, float seconds)
    {
        switch (voice.Kind)
        {
            case VoiceKind::Source:
            case VoiceKind::OneShot:
            {
                const auto a_Source = Sources.find(voice.Entity);
                if (a_Source == Sources.end())
                {
                    voice.Ended = true;

                    return;
                }

                const SourceState& l_Source = a_Source->second;
                voice.Values = l_Source.Values;
                voice.Position = l_Source.Position;
                voice.Velocity = l_Source.Velocity;
                voice.Forward = l_Source.Forward;
                if (voice.Kind == VoiceKind::OneShot)
                {
                    voice.Values.Resource = voice.Resource;
                    voice.Values.Loop = false;
                    voice.Values.Volume *= voice.Scale;
                }

                return;
            }
            case VoiceKind::Attached:
            {
                const Entity l_Entity = TheScene.FindEntityByUUID(voice.Entity);
                if (!l_Entity || !l_Entity.Has<WorldTransformComponent>())
                {
                    voice.Velocity = glm::vec3(0.0f);

                    return;
                }

                const glm::mat4& l_World = l_Entity.Get<WorldTransformComponent>().Matrix;
                const glm::vec3 l_Position(l_World[3]);
                voice.Velocity = voice.Placed && seconds > 0.0f ? (l_Position - voice.Position) / seconds : glm::vec3(0.0f);
                voice.Position = l_Position;
                voice.Forward = GetForward(l_World);
                voice.Placed = true;

                return;
            }
            case VoiceKind::Point:
            {
                voice.Velocity = glm::vec3(0.0f);

                return;
            }
        }
    }

    // In seconds of its clip, or of its sound event at the engine's rate, as its sound has it while real
    double AudioScene::State::GetTime(const Voice& voice) const
    {
        if (voice.Storage == nullptr && !voice.Player)
        {
            return 0.0;
        }

        double l_Cursor = voice.Cursor;
        ma_uint64 l_Frames = 0;
        if (voice.Real != nullptr && voice.Real->HasSound && ma_sound_get_cursor_in_pcm_frames(&voice.Real->Sound, &l_Frames) == MA_SUCCESS)
        {
            l_Cursor = static_cast<double>(l_Frames);
        }

        return l_Cursor / (voice.Player ? GetSampleRate() : voice.Storage->Header.SampleRate);
    }

    // A clip spatialized as a source is by default, which a sound event's own values replace as it loads, seeded by the resource, the entity it follows, if any, and how often the scene has played the resource on its own
    AudioHandle AudioScene::State::PlayOwn(VoiceKind kind, UUID resource, Entity entity, const glm::vec3& position, float volume)
    {
        if (!resource)
        {
            return {};
        }

        AudioSourceComponent l_Values;
        l_Values.Resource = resource;
        l_Values.PlayOnAwake = false;
        l_Values.Spatialize = true;
        l_Values.Volume = volume;
        const UUID l_Entity = entity ? entity.GetUUID() : UUID();
        Voice* l_Voice = NewVoice(kind, l_Entity, resource, l_Values, position, MakeMemory(), SoundEventRandom::MakeSeed(resource, l_Entity, CountOwnPlay(resource)));
        l_Voice->Scale = volume;
        if (entity && entity.Has<WorldTransformComponent>())
        {
            l_Voice->Forward = GetForward(entity.Get<WorldTransformComponent>().Matrix);
        }

        return { l_Voice->Handle };
    }

    // How loud it is as the listener hears it, as voices are ranked and an event's quietest instance found: its volume, occluded, and for a spatialized one its rolloff and cone
    float AudioScene::State::GetLoudness(const Voice& voice) const
    {
        float l_Loudness = voice.Values.Mute ? 0.0f : std::clamp(voice.Values.Volume, 0.0f, 1.0f) * GetOcclusionGain(voice.Values, voice.Occlusion);
        if (voice.Values.Spatialize && HasListener)
        {
            const glm::vec3 l_ToListener = ListenerPosition - voice.Position;
            const float l_Distance = glm::length(l_ToListener);
            l_Loudness *= GetAudioRolloffGain(voice.Values, l_Distance);
            if (l_Distance > 0.0f)
            {
                l_Loudness *= GetConeGain(voice.Values, voice.Forward, l_ToListener / l_Distance);
            }
        }

        return l_Loudness;
    }

    void AudioScene::State::EndVoice(SourceState& source)
    {
        if (Voice* l_Voice = std::exchange(source.Playing, nullptr))
        {
            std::erase(Voices, l_Voice);
            DestroyVoice(Engine, l_Voice);
        }
    }

    // Once the clip's header is in, and its samples, or a sound event's file and every clip it names. A resource that is neither, or a clip whose samples failed, ends it in silence
    void AudioScene::State::Load(Voice& voice)
    {
        if (voice.Loaded || voice.Ended)
        {
            return;
        }

        const AssetState l_State = AssetManager::GetState(voice.Resource);
        if (l_State == AssetState::Loading)
        {
            return;
        }

        const Asset* l_Asset = l_State == AssetState::Ready ? AssetManager::GetAsset(voice.Resource) : nullptr;
        if (l_Asset != nullptr && l_Asset->GetAssetType() == SoundEventAsset::c_AssetType)
        {
            LoadEvent(voice, *static_cast<const SoundEventAsset*>(l_Asset));

            return;
        }

        if (l_Asset == nullptr || l_Asset->GetAssetType() != AudioClipAsset::c_AssetType)
        {
            voice.Ended = true;

            return;
        }

        const Ref<AudioClipStorage>& l_Storage = static_cast<const AudioClipAsset*>(l_Asset)->GetStorage();
        const AudioClipStatus l_Status = l_Storage->Status.load(std::memory_order_acquire);
        if (l_Status == AudioClipStatus::Loading)
        {
            return;
        }

        voice.Ended = l_Status == AudioClipStatus::Failed || l_Storage->Header.Frames == 0;
        voice.Storage = l_Storage;
        voice.Loaded = !voice.Ended;
    }

    // The event's clips are held as its plan is made, and its schedule begins once each has loaded its samples or failed, which plays as nothing, as does a clip that is no clip. Its mix has as many channels as its widest clip
    void AudioScene::State::LoadEvent(Voice& voice, const SoundEventAsset& asset)
    {
        if (!voice.Plan)
        {
            voice.Plan = FindPlan(voice.Resource, asset);
            for (const UUID it_Clip : voice.Plan->Clips)
            {
                voice.EventClips.emplace_back(it_Clip);
            }
        }


        if (!ReadEventClips(voice.EventClips, voice.ClipStorages, voice.ClipTimes, voice.Channels))
        {
            return;
        }

        // One played on its own plays as the event's own source would
        if (voice.Kind == VoiceKind::Point || voice.Kind == VoiceKind::Attached)
        {
            voice.Values = MakeEventValues(voice.Plan->Data, voice.Resource, voice.Scale);
        }

        if (!Admit(voice))
        {
            voice.Ended = true;

            return;
        }

        PrepareParameters(voice);
        voice.Player.emplace(voice.Plan, voice.ClipTimes, voice.Seed, GetSampleRate());
        voice.Loaded = true;
    }

    // Each parameter reads the instance's own target, given what the memory holds, or for a global one the engine's, and starts where its target is, with no seek, or where it was in the version before, if that had it. Room is made in the memory for each, so setting it later allocates nothing
    void AudioScene::State::PrepareParameters(Voice& voice, const SoundEventPlan* previous)
    {
        const auto& l_Parameters = voice.Plan->Data.Parameters;
        const FloatList l_Before = std::move(voice.Heard);
        voice.Targets = TargetList(l_Parameters.size());
        voice.Reads.assign(l_Parameters.size(), nullptr);
        voice.Latest.assign(l_Parameters.size(), 0.0f);
        voice.Heard.assign(l_Parameters.size(), 0.0f);
        voice.HeardFrame = GetClockFrame();
        for (std::size_t it_Parameter = 0; it_Parameter < l_Parameters.size(); ++it_Parameter)
        {
            const SoundParameter& l_Parameter = l_Parameters[it_Parameter];
            const std::string_view l_Name(l_Parameter.Name);
            if (l_Parameter.Global && Engine != nullptr)
            {
                voice.Reads[it_Parameter] = &AudioEngineInternal::FindGlobalParameter(*Engine, l_Name);
            }
            else
            {
                if (const std::optional<float> l_Given = voice.Memory->FindValue(l_Name))
                {
                    voice.Targets[it_Parameter].Set(*l_Given, 0);
                }

                voice.Reads[it_Parameter] = &voice.Targets[it_Parameter];
            }

            voice.Memory->Reserve(l_Name);
            const SoundParameter* l_Was = previous != nullptr ? previous->Data.FindParameter(l_Name) : nullptr;
            const std::size_t l_WasAt = l_Was != nullptr ? static_cast<std::size_t>(l_Was - previous->Data.Parameters.data()) : l_Before.size();
            voice.Heard[it_Parameter] = l_Parameter.Clamp(l_WasAt < l_Before.size() ? l_Before[l_WasAt] : voice.Reads[it_Parameter]->GetWritten().At(voice.HeardFrame).value_or(l_Parameter.Default));
        }

        voice.Nodes.assign(voice.Plan->Nodes.size(), {});
        voice.Pitches.assign(voice.Plan->Nodes.size(), 1.0f);
    }

    // A sound event playing whose asset has loaded a new version since, as Forge saves each edit, moves to it once every clip the new version names has loaded or failed. What it has scheduled plays on, but for a container the new version lacks, a clip it no longer names held for it meanwhile, its parameters are kept by name, and what each container starts from its next trigger is the new version's. A heard voice is made again, as a virtual one made real is, so its mix plays the new version's curves. One fading out plays on as it was
    void AudioScene::State::Refresh(Voice& voice)
    {
        if (!voice.Loaded || !voice.Plan || voice.Ended || voice.Stopping || AssetManager::GetState(voice.Resource) != AssetState::Ready)
        {
            return;
        }

        const Asset* l_Asset = AssetManager::GetAsset(voice.Resource);
        if (l_Asset == nullptr || l_Asset->GetAssetType() != SoundEventAsset::c_AssetType || static_cast<const SoundEventAsset*>(l_Asset)->GetSerial() == voice.Plan->Serial)
        {
            return;
        }

        // Its clips are held from the first frame the new version is seen, so a clip both versions name stays loaded
        const SoundEventAsset& l_Event = *static_cast<const SoundEventAsset*>(l_Asset);
        if (!voice.NextPlan || voice.NextPlan->Serial != l_Event.GetSerial())
        {
            voice.NextPlan = FindPlan(voice.Resource, l_Event);
            voice.NextClips.clear();
            for (const UUID it_Clip : voice.NextPlan->Clips)
            {
                voice.NextClips.emplace_back(it_Clip);
            }
        }

        StorageList l_Storages;
        ClipTimeList l_Times;
        std::uint32_t l_Channels = 1;
        if (!ReadEventClips(voice.NextClips, l_Storages, l_Times, l_Channels))
        {
            return;
        }

        DestroyReal(Engine, voice);
        const Ref<const SoundEventPlan> l_Previous = std::exchange(voice.Plan, std::move(voice.NextPlan));
        voice.NextPlan.reset();
        HoldList l_Holds = std::move(voice.NextClips);
        voice.NextClips.clear();
        const std::size_t l_PlanClips = l_Times.size();
        std::erase_if(voice.Scheduled, [&](SoundEventPlayer::Play& play)
        {
            const UUID l_Container = l_Previous->Data.Containers[play.Node].ID;
            const auto a_Node = std::ranges::find(voice.Plan->Data.Containers, l_Container, &SoundContainer::ID);
            if (a_Node == voice.Plan->Data.Containers.end() || a_Node->Type != SoundContainerType::Clip)
            {
                return true;
            }

            const UUID l_Clip = voice.EventClips[play.Clip].GetID();
            const auto a_Held = std::ranges::find_if(l_Holds, [l_Clip](const AssetHold& hold) { return hold.GetID() == l_Clip; });
            if (a_Held == l_Holds.end())
            {
                l_Holds.emplace_back(l_Clip);
                l_Storages.push_back(voice.ClipStorages[play.Clip]);
                l_Times.push_back(voice.ClipTimes[play.Clip]);
                l_Channels = std::max(l_Channels, voice.ClipStorages[play.Clip] ? voice.ClipStorages[play.Clip]->Header.Channels : 1u);
            }

            play.Node = static_cast<std::uint32_t>(a_Node - voice.Plan->Data.Containers.begin());
            play.Clip = static_cast<std::uint32_t>(std::ranges::find_if(l_Holds, [l_Clip](const AssetHold& hold) { return hold.GetID() == l_Clip; }) - l_Holds.begin());

            return false;
        });

        voice.EventClips = std::move(l_Holds);
        voice.ClipStorages = std::move(l_Storages);
        voice.ClipTimes = std::move(l_Times);
        voice.Channels = l_Channels;
        if (voice.Kind == VoiceKind::Point || voice.Kind == VoiceKind::Attached)
        {
            voice.Values = MakeEventValues(voice.Plan->Data, voice.Resource, voice.Scale);
        }

        PrepareParameters(voice, l_Previous.get());
        voice.Player->Adopt(voice.Plan, std::span<const SoundEventClip>(voice.ClipTimes).first(l_PlanClips), static_cast<std::uint64_t>(voice.Cursor));
        voice.Ends = {};
        voice.NextEnd = 0;
    }

    // On the main thread while the voice is virtual, as its mix would
    void AudioScene::State::SeekParameters(Voice& voice)
    {
        const std::uint64_t l_Frame = GetClockFrame();
        const auto& l_Parameters = voice.Plan->Data.Parameters;
        for (std::size_t it_Parameter = 0; it_Parameter < voice.Heard.size(); ++it_Parameter)
        {
            voice.Heard[it_Parameter] = SeekSoundParameter(l_Parameters[it_Parameter], voice.Heard[it_Parameter], voice.Reads[it_Parameter]->GetWritten(), voice.HeardFrame, l_Frame, GetSampleRate());
        }

        voice.HeardFrame = l_Frame;
    }

    // To every instance playing from the memory, the source's voice and its one-shots, or one played on its own, whose event has the parameter and keeps it as its own, from the frame a change made now starts on. Nothing is allocated
    void AudioScene::State::GiveParameter(const SoundEventMemory& memory, std::string_view name, float value)
    {
        const std::uint64_t l_Frame = GetChangeFrame();
        for (Voice* it_Voice : Voices)
        {
            if (it_Voice->Memory.get() != &memory || !it_Voice->Plan || it_Voice->Targets.empty())
            {
                continue;
            }

            const auto& l_Parameters = it_Voice->Plan->Data.Parameters;
            const auto a_Parameter = std::ranges::find_if(l_Parameters, [name](const SoundParameter& parameter) { return std::string_view(parameter.Name) == name; });
            if (a_Parameter != l_Parameters.end() && !(a_Parameter->Global && Engine != nullptr))
            {
                it_Voice->Targets[static_cast<std::size_t>(a_Parameter - l_Parameters.begin())].Set(value, l_Frame);
            }
        }
    }

    // As its mix has it now while real
    std::optional<float> AudioScene::State::GetHeardParameter(const Voice& voice, std::string_view name) const
    {
        if (!voice.Plan || voice.Heard.empty())
        {
            return std::nullopt;
        }

        const auto& l_Parameters = voice.Plan->Data.Parameters;
        const auto a_Parameter = std::ranges::find_if(l_Parameters, [name](const SoundParameter& parameter) { return std::string_view(parameter.Name) == name; });
        if (a_Parameter == l_Parameters.end())
        {
            return std::nullopt;
        }

        const std::size_t l_Index = static_cast<std::size_t>(a_Parameter - l_Parameters.begin());

        return voice.Real != nullptr && voice.Real->Mix != nullptr ? voice.Real->Mix->GetValue(l_Index) : voice.Heard[l_Index];
    }

    // The play it starts after, among those its mix still has or is yet to be handed, or else where that one ended, so it starts from there. Without either it starts where it was foreseen to
    void AudioScene::State::ResolveAnchor(Voice& voice, SoundEventMixPlay& play, const SoundEventPlayer::Play& scheduled) const
    {
        if (scheduled.After == SoundEventPlayer::c_NoPlay || voice.Real == nullptr)
        {
            return;
        }

        for (const MixPlayList* it_List : { &voice.Real->Plays, &voice.Real->Waiting })
        {
            for (SoundEventMixPlay* it_Play : *it_List)
            {
                if (it_Play != &play && it_Play->Number == scheduled.After)
                {
                    play.After = it_Play;

                    return;
                }
            }
        }

        for (const EndedPlay& it_Ended : voice.Ends)
        {
            if (it_Ended.Number == scheduled.After)
            {
                play.Start = it_Ended.Frame + scheduled.Offset;

                return;
            }
        }
    }

    // The engine's clock, or without an engine the scene's time in frames
    std::uint64_t AudioScene::State::GetClockFrame() const
    {
        return Engine != nullptr ? ma_engine_get_time_in_pcm_frames(&AudioEngineInternal::GetEngine(*Engine)) : static_cast<std::uint64_t>(Time * c_DefaultSampleRate);
    }

    std::uint64_t AudioScene::State::GetChangeFrame() const
    {
        return Engine != nullptr ? AudioEngineInternal::GetChangeFrame(*Engine) : GetClockFrame();
    }

    // As an instance starts, once its event and clips have loaded: none sooner after the event's last than its cooldown, and none beyond its limit, counting every instance of it playing and not fading out, unless it steals one, the oldest, or the quietest as heard when the new one is no quieter, which fades out over 20 ms. One admitted is the event's last to start
    bool AudioScene::State::Admit(const Voice& voice)
    {
        const SoundEventData& l_Data = voice.Plan->Data;
        const auto a_Last = std::ranges::find(LastStarts, voice.Resource, &std::pair<UUID, double>::first);
        if (l_Data.Cooldown > 0.0f && a_Last != LastStarts.end() && Time - a_Last->second < static_cast<double>(l_Data.Cooldown))
        {
            return false;
        }

        const auto a_Counts = [&voice](const Voice* other) { return other != &voice && other->Resource == voice.Resource && other->Plan && other->Loaded && !other->Stopping && !other->Ended; };
        if (l_Data.MaxInstances != 0)
        {
            for (std::size_t l_Playing = static_cast<std::size_t>(std::ranges::count_if(Voices, a_Counts)); l_Playing >= l_Data.MaxInstances; --l_Playing)
            {
                if (l_Data.Steal == SoundStealMode::None)
                {
                    return false;
                }

                Voice* l_Victim = nullptr;
                for (Voice* it_Other : Voices)
                {
                    if (!a_Counts(it_Other))
                    {
                        continue;
                    }

                    const bool l_Older = l_Victim == nullptr || it_Other->Number < l_Victim->Number;
                    if (l_Data.Steal == SoundStealMode::Oldest ? l_Older : l_Victim == nullptr || GetLoudness(*it_Other) < GetLoudness(*l_Victim) || (GetLoudness(*it_Other) == GetLoudness(*l_Victim) && l_Older))
                    {
                        l_Victim = it_Other;
                    }
                }

                if (l_Data.Steal == SoundStealMode::Quietest && GetLoudness(voice) < GetLoudness(*l_Victim))
                {
                    return false;
                }

                StopVoice(*l_Victim, c_StealFadeSeconds);
            }
        }

        if (a_Last != LastStarts.end())
        {
            a_Last->second = Time;
        }
        else
        {
            LastStarts.emplace_back(voice.Resource, Time);
        }

        return true;
    }

    // How often the resource has been played on its own in the scene before
    std::uint64_t AudioScene::State::CountOwnPlay(UUID resource)
    {
        const auto a_Found = std::ranges::find(OwnPlays, resource, &std::pair<UUID, std::uint64_t>::first);
        if (a_Found != OwnPlays.end())
        {
            return a_Found->second++;
        }

        OwnPlays.emplace_back(resource, 1);

        return 0;
    }

    // The most important first, then the loudest, then the first to play. Beyond the virtual voices the least of them stop, and with no listener or no engine none is mixed, nor is one not yet heard that asks to be occluded until its first ray, which keeps its time meanwhile
    void AudioScene::State::ChooseRealVoices()
    {
        std::ranges::sort(Voices, [](const Voice* left, const Voice* right)
        {
            if (left->Values.Priority != right->Values.Priority)
            {
                return left->Values.Priority < right->Values.Priority;
            }

            if (left->Loudness != right->Loudness)
            {
                return left->Loudness > right->Loudness;
            }

            return left->Number < right->Number;
        });

        if (Voices.size() > Settings.VirtualVoices)
        {
            if (!WarnedVoices)
            {
                TR_CORE_WARN("Audio: more than {} sounds play at once, so the least important stop", Settings.VirtualVoices);
                WarnedVoices = true;
            }

            for (std::size_t it_Voice = Settings.VirtualVoices; it_Voice < Voices.size(); ++it_Voice)
            {
                Voices[it_Voice]->Ended = true;
            }
        }

        std::uint32_t l_Real = 0;
        for (Voice* it_Voice : Voices)
        {
            // A paused voice keeps its sound, stopped, so it goes on from exactly where it was, and takes no real voice meanwhile
            if (it_Voice->Paused && !it_Voice->Ended)
            {
                continue;
            }

            // One fading out stays as it is until it has, heard if it was, so its fade is not cut short
            if (it_Voice->Stopping && !it_Voice->Ended)
            {
                if (it_Voice->Real != nullptr)
                {
                    ++l_Real;
                    MakeReal(*it_Voice);
                }

                continue;
            }

            const bool l_Checked = !IsOccluded(it_Voice->Values) || it_Voice->OcclusionFrame != 0 || it_Voice->Real != nullptr;
            const bool l_Mixed = Engine != nullptr && HasListener && it_Voice->Loaded && !it_Voice->Paused && !it_Voice->Ended && l_Checked && l_Real < Settings.RealVoices;
            if (l_Mixed)
            {
                ++l_Real;
                MakeReal(*it_Voice);
            }
            else
            {
                DestroyReal(Engine, *it_Voice);
            }
        }
    }

    // From where its time has got to. A stream is mixed once its first page is in, which offline is at once, so a render gives the same samples each time. A sound event's mix starts at its time, with every play scheduled that has not yet ended, those already under way from where they would be by now
    void AudioScene::State::MakeReal(Voice& voice)
    {
        if (voice.Real == nullptr && voice.Plan)
        {
            RealVoice* l_Real = Memory::New<RealVoice>(MemoryTag::Audio);
            // A virtual voice's time is added up a frame's length at a time, so it is taken to the nearest frame. Its parameters go on from where they were sought to
            l_Real->Mix = Memory::New<SoundEventMix>(MemoryTag::Audio, voice.Channels, GetSampleRate(), static_cast<std::uint64_t>(std::llround(voice.Cursor)));
            l_Real->Mix->Control(*voice.Plan, voice.Reads, voice.Heard, ma_engine_get_node_graph(&AudioEngineInternal::GetEngine(*Engine)), voice.HeardFrame);
            voice.Real = l_Real;
            for (const SoundEventPlayer::Play& it_Play : voice.Scheduled)
            {
                if (SoundEventMixPlay* l_Play = MakeMixPlay(*Engine, voice.ClipStorages[it_Play.Clip], it_Play, *voice.Plan))
                {
                    l_Real->Waiting.push_back(l_Play);
                    ResolveAnchor(voice, *l_Play, it_Play);
                }
            }

            ScheduleEvent(voice);
        }

        if (voice.Real == nullptr)
        {
            RealVoice* l_Real = Memory::New<RealVoice>(MemoryTag::Audio);
            if (!l_Real->Source.Start(voice.Storage))
            {
                Memory::Delete(l_Real);
                voice.Ended = true;

                return;
            }

            const std::uint64_t l_Cursor = std::min(static_cast<std::uint64_t>(voice.Cursor), voice.Storage->Header.Frames);
            if (l_Cursor != 0)
            {
                static_cast<void>(ma_data_source_seek_to_pcm_frame(l_Real->Source.GetDataSource(), l_Cursor));
            }

            if (AudioStream* l_Stream = l_Real->Source.GetStream())
            {
                AudioEngineInternal::AddStream(*Engine, *l_Stream);
                if (Engine->GetOutput() == AudioOutput::Offline)
                {
                    l_Stream->RefillNow();
                }
                else
                {
                    l_Stream->RequestRefill();
                }
            }

            voice.Real = l_Real;
        }

        StartSound(voice);
    }

    void AudioScene::State::StartSound(Voice& voice)
    {
        RealVoice& l_Real = *voice.Real;
        if (l_Real.HasSound)
        {
            AttachOcclusion(voice, true);
            RouteVoice(voice);
            ApplyValues(voice, Settings);

            return;
        }

        if (AudioStream* l_Stream = l_Real.Source.GetStream(); l_Stream != nullptr && !l_Stream->IsPrimed())
        {
            return;
        }

        ma_data_source* l_Source = l_Real.Mix != nullptr ? l_Real.Mix->GetDataSource() : l_Real.Source.GetDataSource();
        if (ma_sound_init_from_data_source(&AudioEngineInternal::GetEngine(*Engine), l_Source, 0, nullptr, &l_Real.Sound) != MA_SUCCESS)
        {
            TR_CORE_ERROR("Audio: the sound of entity {} could not start", voice.Entity);
            voice.Ended = true;

            return;
        }

        l_Real.HasSound = true;
        AttachOcclusion(voice, false);
        RouteVoice(voice);
        ApplyValues(voice, Settings);
        ma_sound_start(&l_Real.Sound);
    }

    // Once its source asks to be occluded, or is still muffled from when it did, then kept until the voice is virtual again, passing the sound through exactly once it is clear, into the sound's group. A sound already heard goes through it from clear, as it was heard, which miniaudio allows while it mixes, and one starting now from where the voice's occlusion is
    void AudioScene::State::AttachOcclusion(Voice& voice, bool heard)
    {
        RealVoice& l_Real = *voice.Real;
        if (!l_Real.HasOcclusion && (IsOccluded(voice.Values) || voice.Occlusion > 0.0f))
        {
            ma_engine& l_Engine = AudioEngineInternal::GetEngine(*Engine);
            OcclusionNode& l_Node = l_Real.Occlusion;
            l_Node.Channels = ma_engine_get_channels(&l_Engine);
            l_Node.SampleRate = static_cast<float>(ma_engine_get_sample_rate(&l_Engine));
            l_Node.Gain = heard ? 1.0f : GetOcclusionGain(voice.Values, voice.Occlusion);
            l_Node.Wet = heard ? 0.0f : voice.Occlusion;
            l_Node.RampGain = l_Node.Gain;
            l_Node.RampWet = l_Node.Wet;
            SetOcclusionCutoff(l_Node, voice.Values.OcclusionCutoff);
            ApplyOcclusion(voice, FrameSeconds);

            const std::array<ma_uint32, 1> l_Channels{ l_Node.Channels };
            ma_node_config l_Config = ma_node_config_init();
            l_Config.vtable = &c_OcclusionVTable;
            l_Config.pInputChannels = l_Channels.data();
            l_Config.pOutputChannels = l_Channels.data();
            if (ma_node_init(ma_engine_get_node_graph(&l_Engine), &l_Config, &l_Engine.allocationCallbacks, &l_Node.Base) != MA_SUCCESS)
            {
                TR_CORE_ERROR("Audio: the occlusion of entity {} could not start, so it is heard clear", voice.Entity);

                return;
            }

            // Into the sound's group before the sound into it, so nothing it plays is lost between
            l_Real.HasOcclusion = true;
            RouteVoice(voice);
            static_cast<void>(ma_node_attach_output_bus(&l_Real.Sound, 0, &l_Node.Base, 0));
        }

        if (l_Real.HasOcclusion)
        {
            ApplyOcclusion(voice, FrameSeconds);
        }
    }

    // Into the mixer's group its source names, or the sound event's when it names none, or Master, through its occlusion node once it has one, and again whenever either changes. A new sound waits in the engine's output until then, not yet started
    void AudioScene::State::RouteVoice(Voice& voice)
    {
        RealVoice& l_Real = *voice.Real;
        ma_node* l_Node = l_Real.HasOcclusion ? static_cast<ma_node*>(&l_Real.Occlusion.Base) : static_cast<ma_node*>(&l_Real.Sound);
        const UUID l_Output = !voice.Values.Output && voice.Plan ? voice.Plan->Data.Output : voice.Values.Output;
        if (l_Node == l_Real.Routed && l_Output == l_Real.RoutedGroup)
        {
            return;
        }

        if (l_Real.Routed != nullptr && l_Real.Routed != l_Node)
        {
            AudioEngineInternal::Unroute(*Engine, l_Real.Routed);
        }

        AudioEngineInternal::Route(*Engine, l_Node, l_Output);
        l_Real.Routed = l_Node;
        l_Real.RoutedGroup = l_Output;
    }

    // Everything its schedule starts before a little ahead of where it plays, which for a real voice is handed to its mix as each is made, those that find its queue full waiting for the next frame, and those it has played taken back, unless one it has yet to finish starts after it. Once nothing is left to schedule and every play is handed over, its mix is told so, and ends after its last. The plays scheduled are kept until they end, as a virtual voice made real again needs them. Its parameters are read from its mix first, a Switch picks by the latest value given, and in an event whose curves move a pitch, its clips playing are foreseen to end at the pitch the curves give them now
    void AudioScene::State::ScheduleEvent(Voice& voice)
    {
        if (!voice.Player || voice.Ended)
        {
            return;
        }

        RealVoice* l_Real = voice.Real;
        if (l_Real != nullptr && l_Real->Mix != nullptr)
        {
            for (std::size_t it_Parameter = 0; it_Parameter < voice.Heard.size(); ++it_Parameter)
            {
                voice.Heard[it_Parameter] = l_Real->Mix->GetValue(it_Parameter);
            }

            voice.HeardFrame = GetClockFrame();
            const auto a_Followed = [l_Real](const SoundEventMixPlay* play)
            {
                const auto a_Follows = [play](const SoundEventMixPlay* other) { return other->After == play && !other->Done.load(std::memory_order_acquire); };

                return std::ranges::any_of(l_Real->Plays, a_Follows) || std::ranges::any_of(l_Real->Waiting, a_Follows);
            };

            std::erase_if(l_Real->Plays, [this, &voice, &a_Followed](SoundEventMixPlay* play)
            {
                if (!play->Done.load(std::memory_order_acquire) || a_Followed(play))
                {
                    return false;
                }

                voice.Ends[voice.NextEnd++ % c_EndsKept] = { play->Number, play->Ended };
                DestroyMixPlay(Engine, play);

                return true;
            });
        }

        if (voice.Plan->HasPitchCurves)
        {
            voice.Plan->Evaluate(voice.Heard, voice.Nodes);
            for (std::size_t it_Node = 0; it_Node < voice.Nodes.size(); ++it_Node)
            {
                voice.Pitches[it_Node] = voice.Nodes[it_Node].Pitch;
            }

            voice.Player->Retime(voice.Pitches, static_cast<std::uint64_t>(voice.Cursor), voice.Scheduled);
        }

        for (std::size_t it_Parameter = 0; it_Parameter < voice.Latest.size(); ++it_Parameter)
        {
            const SoundParameter& l_Parameter = voice.Plan->Data.Parameters[it_Parameter];
            const SoundParameterTarget::Reading& l_Target = voice.Reads[it_Parameter]->GetWritten();
            voice.Latest[it_Parameter] = l_Parameter.Clamp(l_Target.Given ? l_Target.Value : l_Parameter.Default);
        }

        const double l_Speed = std::max(std::clamp(voice.Values.Pitch, c_MinimumPitch, c_MaximumPitch), 1.0f) * 2.0;
        const double l_Ahead = (2.0 * FrameSeconds + c_EventLookahead) * GetSampleRate() * l_Speed;
        const std::size_t l_Before = voice.Scheduled.size();
        voice.Player->Schedule(static_cast<std::uint64_t>(voice.Cursor + l_Ahead) + 1, *voice.Memory, voice.Scheduled, voice.Latest);
        if (l_Real != nullptr && l_Real->Mix != nullptr)
        {
            for (std::size_t it_Play = l_Before; it_Play < voice.Scheduled.size(); ++it_Play)
            {
                if (SoundEventMixPlay* l_Play = MakeMixPlay(*Engine, voice.ClipStorages[voice.Scheduled[it_Play].Clip], voice.Scheduled[it_Play], *voice.Plan))
                {
                    l_Real->Waiting.push_back(l_Play);
                    ResolveAnchor(voice, *l_Play, voice.Scheduled[it_Play]);
                }
            }

            std::size_t l_Offered = 0;
            while (l_Offered < l_Real->Waiting.size() && l_Real->Mix->Offer(*l_Real->Waiting[l_Offered]))
            {
                l_Real->Plays.push_back(l_Real->Waiting[l_Offered++]);
            }

            l_Real->Waiting.erase(l_Real->Waiting.begin(), l_Real->Waiting.begin() + static_cast<std::ptrdiff_t>(l_Offered));
            if (l_Real->Plays.size() + l_Real->Waiting.size() > SoundEventMix::c_MaxPlays && !WarnedPlays)
            {
                TR_CORE_WARN("Audio: a sound event of entity {} plays more than {} clips at once, so those beyond them are not heard", voice.Entity, SoundEventMix::c_MaxPlays);
                WarnedPlays = true;
            }

            if (voice.Player->IsFinished() && l_Real->Waiting.empty())
            {
                l_Real->Mix->Finish();
            }
        }

        const std::uint64_t l_Now = static_cast<std::uint64_t>(voice.Cursor);
        std::erase_if(voice.Scheduled, [l_Now](const SoundEventPlayer::Play& play) { return play.GetEnd() <= l_Now; });
    }

    std::uint32_t AudioScene::State::GetSampleRate() const
    {
        return Engine != nullptr ? Engine->GetSampleRate() : c_DefaultSampleRate;
    }

    // The plan of the version the asset has now, made once and shared by its voices, which a version before it is replaced by
    Ref<const SoundEventPlan> AudioScene::State::FindPlan(UUID event, const SoundEventAsset& asset)
    {
        const auto a_Found = std::ranges::find_if(Plans, [event](const Ref<const SoundEventPlan>& plan) { return plan->Event == event; });
        if (a_Found != Plans.end() && (*a_Found)->Serial == asset.GetSerial())
        {
            return *a_Found;
        }

        Ref<const SoundEventPlan> l_Plan = SoundEventPlan::Make(event, asset);
        if (a_Found != Plans.end())
        {
            *a_Found = l_Plan;
        }
        else
        {
            Plans.push_back(l_Plan);
        }

        return l_Plan;
    }

    // A real voice's time is its sound's, and a virtual voice's moves on at its clip's rate and its pitch, or a sound event's at the engine's rate, ending once its schedule has
    void AudioScene::State::Advance(Voice& voice, float seconds)
    {
        if (!voice.Loaded || voice.Ended)
        {
            return;
        }

        if (voice.Real != nullptr && voice.Real->HasSound)
        {
            ma_uint64 l_Cursor = 0;
            if (ma_sound_get_cursor_in_pcm_frames(&voice.Real->Sound, &l_Cursor) == MA_SUCCESS)
            {
                voice.Cursor = static_cast<double>(l_Cursor);
            }

            // One fading out ends as its sound stops
            voice.Ended = ma_sound_at_end(&voice.Real->Sound) == MA_TRUE || (voice.Stopping && ma_sound_is_playing(&voice.Real->Sound) == MA_FALSE);

            return;
        }

        if (voice.Paused || voice.Real != nullptr)
        {
            return;
        }

        voice.StopLeft -= voice.Stopping ? seconds : 0.0f;
        if (voice.Stopping && voice.StopLeft <= 0.0f)
        {
            voice.Ended = true;

            return;
        }

        if (voice.Plan)
        {
            SeekParameters(voice);
            voice.Cursor += static_cast<double>(seconds) * GetSampleRate() * std::clamp(voice.Values.Pitch, c_MinimumPitch, c_MaximumPitch);
            voice.Ended = voice.Player->IsFinished() && voice.Cursor >= static_cast<double>(voice.Player->GetEnd());

            return;
        }

        const AudioClipHeader& l_Header = voice.Storage->Header;
        const double l_Frames = static_cast<double>(l_Header.Frames);
        voice.Cursor += static_cast<double>(seconds) * l_Header.SampleRate * std::clamp(voice.Values.Pitch, c_MinimumPitch, c_MaximumPitch);
        if (voice.Cursor >= l_Frames)
        {
            if (voice.Values.Loop)
            {
                voice.Cursor = std::fmod(voice.Cursor, l_Frames);
            }
            else
            {
                voice.Ended = true;
            }
        }
    }

    void AudioScene::State::RemoveEnded()
    {
        for (Voice* it_Voice : Voices)
        {
            if (!it_Voice->Ended)
            {
                continue;
            }

            if (const auto a_Source = Sources.find(it_Voice->Entity); a_Source != Sources.end() && a_Source->second.Playing == it_Voice)
            {
                a_Source->second.Playing = nullptr;
            }

            DestroyVoice(Engine, it_Voice);
        }

        std::erase_if(Voices, [](const Voice* voice) { return voice->Ended; });
    }

    AudioScene::AudioScene(Scene& scene, AudioEngine* engine, const AudioSettings& settings) : m_State(Memory::New<State>(MemoryTag::Audio, scene, engine, settings))
    {

    }

    AudioScene::~AudioScene()
    {
        for (Voice* it_Voice : m_State->Voices)
        {
            DestroyVoice(m_State->Engine, it_Voice);
        }

        Memory::Delete(m_State);
    }

    void AudioScene::SetPhysics(const PhysicsScene3D* physics, const PhysicsScene2D* physics2D)
    {
        m_State->Physics = physics;
        m_State->Physics2D = physics2D;
    }

    // Sources are read before the listener's move is known to their voices, so a source and its listener moving together read as still
    void AudioScene::Update(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's audio updates on the main thread.");

        State& l_State = *m_State;
        const float l_Seconds = std::max(timestep.GetSeconds(), 0.0f);
        ++l_State.Frame;
        l_State.FrameSeconds = l_Seconds;
        l_State.Time += m_Paused ? 0.0 : static_cast<double>(l_Seconds);
        l_State.UpdateListener(l_Seconds);

        SceneRegistry& l_Registry = l_State.TheScene.GetRegistry();
        for (const auto [it_Handle, it_Source, it_ID, it_World] : l_Registry.view<AudioSourceComponent, IDComponent, WorldTransformComponent>().each())
        {
            SourceState& l_Source = l_State.Sources.try_emplace(it_ID.ID).first->second;
            l_Source.Seen = l_State.Frame;

            const glm::vec3 l_Position(it_World.Matrix[3]);
            l_Source.Velocity = l_Source.Placed && l_Seconds > 0.0f ? (l_Position - l_Source.Position) / l_Seconds : glm::vec3(0.0f);
            l_Source.Position = l_Position;
            l_Source.Forward = GetForward(it_World.Matrix);
            l_Source.Values = it_Source;
            l_Source.Placed = true;

            if (!std::exchange(l_Source.Awake, true) && it_Source.PlayOnAwake)
            {
                l_State.StartVoice(l_Source, it_ID.ID, it_Source);
            }

            // A source given another clip stops, as Unity's does
            if (l_Source.Playing != nullptr && it_Source.Resource != l_Source.Playing->Resource)
            {
                l_State.EndVoice(l_Source);
            }
        }

        // The sources gone since the last frame, with their entities or on their own, stop
        for (auto it_Source = l_State.Sources.begin(); it_Source != l_State.Sources.end();)
        {
            if (it_Source->second.Seen == l_State.Frame)
            {
                ++it_Source;

                continue;
            }

            l_State.EndVoice(it_Source->second);
            it_Source = l_State.Sources.erase(it_Source);
        }

        // Every voice where it plays from, before any is checked, loaded or ranked, a one-shot of a source gone ending with it
        for (Voice* it_Voice : l_State.Voices)
        {
            l_State.PlaceVoice(*it_Voice, l_Seconds);
        }

        // Paused, nothing is checked and nothing eases, so a Step plays none of it
        if (!m_Paused)
        {
            l_State.UpdateOcclusion(l_Seconds);
        }
        else
        {
            l_State.OcclusionRays = 0;
        }

        for (Voice* it_Voice : l_State.Voices)
        {
            l_State.Load(*it_Voice);
            l_State.Refresh(*it_Voice);
            it_Voice->Loudness = l_State.GetLoudness(*it_Voice);
        }

        // Paused, every voice stays as it is, and its time with it
        if (!m_Paused)
        {
            l_State.ChooseRealVoices();
            for (Voice* it_Voice : l_State.Voices)
            {
                l_State.Advance(*it_Voice, l_Seconds);
            }

            // Once each knows where it plays, so a sound event schedules ahead of that
            for (Voice* it_Voice : l_State.Voices)
            {
                if (!it_Voice->Paused)
                {
                    l_State.ScheduleEvent(*it_Voice);
                }
            }
        }

        l_State.RemoveEnded();
        std::erase_if(l_State.Plans, [](const Ref<const SoundEventPlan>& plan) { return plan.use_count() == 1; });
    }

    void AudioScene::Play(Entity entity)
    {
        if (!entity.IsValid() || !entity.Has<AudioSourceComponent>())
        {
            return;
        }

        State& l_State = *m_State;
        SourceState& l_Source = l_State.FindSource(entity);
        l_Source.Awake = true;
        l_State.StartVoice(l_Source, entity.GetUUID(), entity.Get<AudioSourceComponent>());
    }

    void AudioScene::Stop(Entity entity)
    {
        if (!entity.IsValid())
        {
            return;
        }

        State& l_State = *m_State;
        if (const auto a_Found = l_State.Sources.find(entity.GetUUID()); a_Found != l_State.Sources.end())
        {
            l_State.EndVoice(a_Found->second);
        }

        std::erase_if(l_State.Voices, [&l_State, entity](Voice* voice)
        {
            if (voice->Kind != VoiceKind::OneShot || voice->Entity != entity.GetUUID())
            {
                return false;
            }

            DestroyVoice(l_State.Engine, voice);

            return true;
        });
    }

    // At once, so the sounds stop this frame, and their time stays where they stopped. A one-shot fading out ends instead
    void AudioScene::Pause(Entity entity)
    {
        if (!entity.IsValid())
        {
            return;
        }

        for (Voice* it_Voice : m_State->Voices)
        {
            if (it_Voice->Entity != entity.GetUUID() || (it_Voice->Kind != VoiceKind::Source && it_Voice->Kind != VoiceKind::OneShot) || it_Voice->Paused)
            {
                continue;
            }

            it_Voice->Paused = true;
            it_Voice->Ended = it_Voice->Ended || it_Voice->Stopping;
            if (it_Voice->Real != nullptr && it_Voice->Real->HasSound)
            {
                ma_sound_stop(&it_Voice->Real->Sound);
            }
        }
    }

    // A voice that kept its sound starts again at once, unless the whole scene is paused, and one that did not is mixed again as the voices are next chosen
    void AudioScene::UnPause(Entity entity)
    {
        if (!entity.IsValid())
        {
            return;
        }

        for (Voice* it_Voice : m_State->Voices)
        {
            if (it_Voice->Entity != entity.GetUUID() || (it_Voice->Kind != VoiceKind::Source && it_Voice->Kind != VoiceKind::OneShot) || !it_Voice->Paused)
            {
                continue;
            }

            it_Voice->Paused = false;
            if (!m_Paused && it_Voice->Real != nullptr && it_Voice->Real->HasSound)
            {
                ma_sound_start(&it_Voice->Real->Sound);
            }
        }
    }

    bool AudioScene::IsPlaying(Entity entity) const
    {
        const auto a_Found = entity.IsValid() ? m_State->Sources.find(entity.GetUUID()) : m_State->Sources.end();
        const Voice* l_Voice = a_Found != m_State->Sources.end() ? a_Found->second.Playing : nullptr;

        return l_Voice != nullptr && !l_Voice->Paused && !l_Voice->Ended;
    }

    double AudioScene::GetTime(Entity entity) const
    {
        const auto a_Found = entity.IsValid() ? m_State->Sources.find(entity.GetUUID()) : m_State->Sources.end();

        return a_Found != m_State->Sources.end() && a_Found->second.Playing != nullptr ? m_State->GetTime(*a_Found->second.Playing) : 0.0;
    }

    bool AudioScene::IsVirtual(Entity entity) const
    {
        const auto a_Found = entity.IsValid() ? m_State->Sources.find(entity.GetUUID()) : m_State->Sources.end();
        const Voice* l_Voice = a_Found != m_State->Sources.end() ? a_Found->second.Playing : nullptr;

        return l_Voice != nullptr && (l_Voice->Paused || l_Voice->Real == nullptr || !l_Voice->Real->HasSound);
    }

    std::optional<AudioScene::Occlusion> AudioScene::GetOcclusion(Entity entity) const
    {
        const auto a_Found = entity.IsValid() ? m_State->Sources.find(entity.GetUUID()) : m_State->Sources.end();
        const Voice* l_Voice = a_Found != m_State->Sources.end() ? a_Found->second.Playing : nullptr;
        if (l_Voice == nullptr || l_Voice->OcclusionFrame == 0 || (!IsOccluded(l_Voice->Values) && l_Voice->Occlusion <= 0.0f))
        {
            return std::nullopt;
        }

        return Occlusion{ l_Voice->Occlusion, l_Voice->Blocked, l_Voice->RayFrom, l_Voice->RayTo, l_Voice->RayHit };
    }

    // The sounds stop where they are and start from there again, as a stopped sound of miniaudio's keeps its place
    void AudioScene::SetPaused(bool paused)
    {
        if (paused == m_Paused)
        {
            return;
        }

        m_Paused = paused;
        m_State->ScenePaused = paused;
        for (Voice* it_Voice : m_State->Voices)
        {
            // A fade would start over once the scene goes on, so one fading out ends instead
            it_Voice->Ended = it_Voice->Ended || (paused && it_Voice->Stopping);
            if (it_Voice->Real == nullptr || !it_Voice->Real->HasSound)
            {
                continue;
            }

            if (paused)
            {
                ma_sound_stop(&it_Voice->Real->Sound);
            }
            else if (!it_Voice->Paused)
            {
                ma_sound_start(&it_Voice->Real->Sound);
            }
        }
    }

    // The voices go on as they are, mixed or not, so a kept sound has no gap: a source's own, its one-shots and those following the entity. One played at a point stays with its scene. The scenes share one engine
    AudioCarry AudioScene::Extract(std::span<const UUID> entities)
    {
        State& l_State = *m_State;
        AudioCarry l_Carry;
        l_Carry.m_State = Memory::New<AudioCarry::State>(MemoryTag::Audio);
        l_Carry.m_State->Engine = l_State.Engine;
        std::erase_if(l_State.Voices, [&l_Carry, entities](Voice* voice)
        {
            if (voice->Kind == VoiceKind::Point || std::ranges::find(entities, voice->Entity) == entities.end())
            {
                return false;
            }

            l_Carry.m_State->Voices.push_back(voice);

            return true;
        });

        for (const UUID it_Entity : entities)
        {
            const auto a_Found = l_State.Sources.find(it_Entity);
            if (a_Found == l_State.Sources.end())
            {
                continue;
            }

            l_Carry.m_State->Sources.emplace_back(it_Entity, a_Found->second);
            l_State.Sources.erase(a_Found);
        }

        return l_Carry;
    }

    void AudioScene::Adopt(AudioCarry&& carry)
    {
        AudioCarry l_Carry(std::move(carry));
        if (l_Carry.m_State == nullptr)
        {
            return;
        }

        State& l_State = *m_State;
        TR_CORE_ASSERT(l_Carry.m_State->Engine == l_State.Engine, "Kept sounds go on in the engine they began in.");

        for (auto& [it_Entity, it_Source] : l_Carry.m_State->Sources)
        {
            it_Source.Seen = l_State.Frame;
            l_State.Sources.insert_or_assign(it_Entity, it_Source);
        }

        for (Voice* it_Voice : l_Carry.m_State->Voices)
        {
            it_Voice->Number = l_State.NextNumber++;
            l_State.Voices.push_back(it_Voice);
        }

        l_Carry.m_State->Voices.clear();
    }

    AudioScene::Statistics AudioScene::GetStatistics() const
    {
        Statistics l_Statistics;
        l_Statistics.OcclusionRays = m_State->OcclusionRays;
        for (const Voice* it_Voice : m_State->Voices)
        {
            l_Statistics.Events += it_Voice->Plan ? 1u : 0u;
            if (!it_Voice->Paused && it_Voice->Real != nullptr && it_Voice->Real->HasSound)
            {
                ++l_Statistics.Real;
                l_Statistics.Streams += it_Voice->Real->Source.GetStream() != nullptr ? 1u : 0u;
                l_Statistics.Streams += static_cast<std::uint32_t>(std::ranges::count_if(it_Voice->Real->Plays, [](SoundEventMixPlay* play) { return play->Voice.GetStream() != nullptr; }));
            }
            else
            {
                ++l_Statistics.Virtual;
            }
        }

        return l_Statistics;
    }

    bool AudioScene::SetParameter(Entity entity, std::string_view name, float value)
    {
        if (!entity.IsValid() || !entity.Has<AudioSourceComponent>())
        {
            return false;
        }

        SoundEventMemory& l_Memory = *m_State->FindSource(entity).Memory;
        l_Memory.SetValue(name, value);
        m_State->GiveParameter(l_Memory, name, value);

        return true;
    }

    bool AudioScene::SetParameterLabel(Entity entity, std::string_view name, std::string_view label)
    {
        if (!entity.IsValid() || !entity.Has<AudioSourceComponent>())
        {
            return false;
        }

        const UUID l_Resource = entity.Get<AudioSourceComponent>().Resource;
        const Asset* l_Asset = AssetManager::GetState(l_Resource) == AssetState::Ready ? AssetManager::GetAsset(l_Resource) : nullptr;
        const SoundParameter* l_Parameter = l_Asset != nullptr && l_Asset->GetAssetType() == SoundEventAsset::c_AssetType ? static_cast<const SoundEventAsset*>(l_Asset)->GetData().FindParameter(name) : nullptr;
        const auto a_Label = l_Parameter != nullptr ? std::ranges::find(l_Parameter->Labels, label) : decltype(SoundParameter::Labels)::const_iterator();
        if (l_Parameter == nullptr || a_Label == l_Parameter->Labels.end())
        {
            return false;
        }

        return SetParameter(entity, name, static_cast<float>(a_Label - l_Parameter->Labels.begin()));
    }

    std::optional<float> AudioScene::GetParameter(Entity entity, std::string_view name) const
    {
        const auto a_Found = entity.IsValid() ? m_State->Sources.find(entity.GetUUID()) : m_State->Sources.end();
        if (a_Found == m_State->Sources.end())
        {
            return std::nullopt;
        }

        if (const std::optional<float> l_Value = a_Found->second.Memory ? a_Found->second.Memory->FindValue(name) : std::nullopt)
        {
            return l_Value;
        }

        const Voice* l_Voice = a_Found->second.Playing;
        const SoundParameter* l_Parameter = l_Voice != nullptr && l_Voice->Plan ? l_Voice->Plan->Data.FindParameter(name) : nullptr;

        return l_Parameter != nullptr ? std::optional(l_Parameter->Default) : std::nullopt;
    }

    std::optional<float> AudioScene::GetHeardParameter(Entity entity, std::string_view name) const
    {
        const auto a_Found = entity.IsValid() ? m_State->Sources.find(entity.GetUUID()) : m_State->Sources.end();

        return a_Found != m_State->Sources.end() && a_Found->second.Playing != nullptr ? m_State->GetHeardParameter(*a_Found->second.Playing, name) : std::nullopt;
    }

    // Not woken by it, so a source that Plays On Awake still does
    AudioHandle AudioScene::PlayOneShot(Entity entity, UUID resource, float volumeScale)
    {
        if (!entity.IsValid() || !entity.Has<AudioSourceComponent>())
        {
            return {};
        }

        State& l_State = *m_State;
        SourceState& l_Source = l_State.FindSource(entity);
        const UUID l_Resource = resource ? resource : entity.Get<AudioSourceComponent>().Resource;
        if (!l_Resource)
        {
            return {};
        }

        AudioSourceComponent l_Values = entity.Get<AudioSourceComponent>();
        l_Values.Resource = l_Resource;
        l_Values.Loop = false;
        l_Values.Volume *= volumeScale;
        Voice* l_Voice = l_State.NewVoice(VoiceKind::OneShot, entity.GetUUID(), l_Resource, l_Values, l_Source.Position, l_Source.Memory, SoundEventRandom::MakeSeed(l_Resource, entity.GetUUID(), l_Source.Memory->Plays++));
        l_Voice->Scale = volumeScale;
        l_Voice->Forward = l_Source.Forward;

        return { l_Voice->Handle };
    }

    AudioHandle AudioScene::PlayAtPoint(UUID resource, const glm::vec3& position, float volume)
    {
        return m_State->PlayOwn(VoiceKind::Point, resource, Entity(), position, volume);
    }

    AudioHandle AudioScene::PlayAttached(UUID resource, Entity entity, float volume)
    {
        if (!entity.IsValid() || !entity.Has<WorldTransformComponent>())
        {
            return {};
        }

        return m_State->PlayOwn(VoiceKind::Attached, resource, entity, glm::vec3(entity.Get<WorldTransformComponent>().Matrix[3]), volume);
    }

    void AudioScene::Stop(AudioHandle handle, float fadeSeconds)
    {
        Voice* l_Voice = m_State->FindVoice(handle);
        if (l_Voice == nullptr)
        {
            return;
        }

        if (fadeSeconds <= 0.0f)
        {
            m_State->RemoveVoice(l_Voice);

            return;
        }

        m_State->StopVoice(*l_Voice, fadeSeconds);
    }

    bool AudioScene::IsPlaying(AudioHandle handle) const
    {
        const Voice* l_Voice = m_State->FindVoice(handle);

        return l_Voice != nullptr && !l_Voice->Paused;
    }

    double AudioScene::GetTime(AudioHandle handle) const
    {
        const Voice* l_Voice = m_State->FindVoice(handle);

        return l_Voice != nullptr ? m_State->GetTime(*l_Voice) : 0.0;
    }

    bool AudioScene::SetParameter(AudioHandle handle, std::string_view name, float value)
    {
        Voice* l_Voice = m_State->FindVoice(handle);
        if (l_Voice == nullptr)
        {
            return false;
        }

        l_Voice->Memory->SetValue(name, value);
        m_State->GiveParameter(*l_Voice->Memory, name, value);

        return true;
    }

    // By its event as it plays it, or as it has loaded while the voice waits for it
    bool AudioScene::SetParameterLabel(AudioHandle handle, std::string_view name, std::string_view label)
    {
        const Voice* l_Voice = m_State->FindVoice(handle);
        if (l_Voice == nullptr)
        {
            return false;
        }

        const Asset* l_Asset = l_Voice->Plan == nullptr && AssetManager::GetState(l_Voice->Resource) == AssetState::Ready ? AssetManager::GetAsset(l_Voice->Resource) : nullptr;
        const SoundEventData* l_Data = l_Voice->Plan ? &l_Voice->Plan->Data : (l_Asset != nullptr && l_Asset->GetAssetType() == SoundEventAsset::c_AssetType ? &static_cast<const SoundEventAsset*>(l_Asset)->GetData() : nullptr);
        const SoundParameter* l_Parameter = l_Data != nullptr ? l_Data->FindParameter(name) : nullptr;
        const auto a_Label = l_Parameter != nullptr ? std::ranges::find(l_Parameter->Labels, label) : decltype(SoundParameter::Labels)::const_iterator();
        if (l_Parameter == nullptr || a_Label == l_Parameter->Labels.end())
        {
            return false;
        }

        return SetParameter(handle, name, static_cast<float>(a_Label - l_Parameter->Labels.begin()));
    }

    std::optional<float> AudioScene::GetParameter(AudioHandle handle, std::string_view name) const
    {
        const Voice* l_Voice = m_State->FindVoice(handle);
        if (l_Voice == nullptr)
        {
            return std::nullopt;
        }

        if (const std::optional<float> l_Value = l_Voice->Memory->FindValue(name))
        {
            return l_Value;
        }

        const SoundParameter* l_Parameter = l_Voice->Plan ? l_Voice->Plan->Data.FindParameter(name) : nullptr;

        return l_Parameter != nullptr ? std::optional(l_Parameter->Default) : std::nullopt;
    }

    std::optional<float> AudioScene::GetHeardParameter(AudioHandle handle, std::string_view name) const
    {
        const Voice* l_Voice = m_State->FindVoice(handle);

        return l_Voice != nullptr ? m_State->GetHeardParameter(*l_Voice, name) : std::nullopt;
    }

    // Moved there at once, so its velocity is none
    void AudioScene::SetPosition(AudioHandle handle, const glm::vec3& position)
    {
        Voice* l_Voice = m_State->FindVoice(handle);
        if (l_Voice == nullptr || (l_Voice->Kind != VoiceKind::Point && l_Voice->Kind != VoiceKind::Attached))
        {
            return;
        }

        l_Voice->Kind = VoiceKind::Point;
        l_Voice->Entity = UUID();
        l_Voice->Position = position;
        l_Voice->Velocity = glm::vec3(0.0f);
    }

    // From the entity's place on the next update, which its velocity is first taken after
    void AudioScene::Follow(AudioHandle handle, Entity entity)
    {
        Voice* l_Voice = m_State->FindVoice(handle);
        if (l_Voice == nullptr || (l_Voice->Kind != VoiceKind::Point && l_Voice->Kind != VoiceKind::Attached) || !entity.IsValid())
        {
            return;
        }

        l_Voice->Kind = VoiceKind::Attached;
        l_Voice->Entity = entity.GetUUID();
        l_Voice->Placed = false;
    }

    bool AudioScene::SetGlobalParameter(std::string_view name, float value)
    {
        if (m_State->Engine == nullptr)
        {
            return false;
        }

        m_State->Engine->SetGlobalParameter(name, value);

        return true;
    }

    std::optional<float> AudioScene::GetGlobalParameter(std::string_view name) const
    {
        return m_State->Engine != nullptr ? m_State->Engine->GetGlobalParameter(name) : std::nullopt;
    }

    AudioMixer* AudioScene::GetMixer() const
    {
        return m_State->Engine != nullptr ? &m_State->Engine->GetMixer() : nullptr;
    }

    const AudioSettings& AudioScene::GetSettings() const
    {
        return m_State->Settings;
    }
}