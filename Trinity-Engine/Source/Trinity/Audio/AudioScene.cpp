#include "Trinity/Audio/AudioScene.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Audio/AudioClipAsset.hpp"
#include "Trinity/Audio/AudioClipStorage.hpp"
#include "Trinity/Audio/AudioEngine.hpp"
#include "Trinity/Audio/AudioEngineInternal.hpp"
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

        // The part of a voice that is mixed: its clip read through a voice of its own, the sound miniaudio mixes that in, once its source asks to be occluded the node after the sound, and which of the two plays into the mixer's group the source names. Only a real voice has one, since a virtual voice needs only its time
        struct RealVoice
        {
            AudioClipVoice Source;
            ma_sound Sound{};
            bool HasSound = false;
            OcclusionNode Occlusion;
            bool HasOcclusion = false;
            ma_node* Routed = nullptr;
            UUID RoutedGroup;
        };

        // One playing source: its clip, held while it plays, where it is in it, and the source's values as last read
        struct Voice
        {
            UUID Entity;
            UUID Resource;
            AssetRef<AudioClipAsset> Clip;
            Ref<AudioClipStorage> Storage;
            RealVoice* Real = nullptr;
            AudioSourceComponent Values;
            glm::vec3 Position{ 0.0f };
            glm::vec3 Forward{ 0.0f, 0.0f, -1.0f };
            glm::vec3 Velocity{ 0.0f };
            // In the clip's frames
            double Cursor = 0.0;
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

        // A source the scene has had, seen each frame it still has it, with where it was, for its velocity
        struct SourceState
        {
            Voice* Playing = nullptr;
            std::uint64_t Seen = 0;
            glm::vec3 Position{ 0.0f };
            bool Placed = false;
        };

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

            if (l_Real->HasOcclusion)
            {
                ma_node_uninit(&l_Real->Occlusion.Base, &AudioEngineInternal::GetEngine(*engine).allocationCallbacks);
            }

            if (AudioStream* l_Stream = l_Real->Source.GetStream(); l_Stream != nullptr && engine != nullptr)
            {
                AudioEngineInternal::RemoveStream(*engine, *l_Stream);
            }

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
            ma_sound_set_looping(&l_Sound, l_Values.Loop ? MA_TRUE : MA_FALSE);
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
        void StartVoice(SourceState& source, UUID entity, const AudioSourceComponent& values);
        void EndVoice(SourceState& source);
        void Load(Voice& voice);
        void ChooseRealVoices();
        void MakeReal(Voice& voice);
        void StartSound(Voice& voice);
        void AttachOcclusion(Voice& voice, bool heard);
        void RouteVoice(Voice& voice);
        void Advance(Voice& voice, float seconds);
        void RemoveEnded();

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

    // Held from now on, so its clip loads if nothing else has it, and heard once it has
    void AudioScene::State::StartVoice(SourceState& source, UUID entity, const AudioSourceComponent& values)
    {
        EndVoice(source);
        if (!values.Resource)
        {
            return;
        }

        Voice* l_Voice = Memory::New<Voice>(MemoryTag::Audio);
        l_Voice->Entity = entity;
        l_Voice->Resource = values.Resource;
        l_Voice->Clip = AssetRef<AudioClipAsset>(values.Resource);
        l_Voice->Values = values;
        l_Voice->Position = source.Position;
        l_Voice->Number = NextNumber++;
        Voices.push_back(l_Voice);
        source.Playing = l_Voice;
    }

    void AudioScene::State::EndVoice(SourceState& source)
    {
        if (Voice* l_Voice = std::exchange(source.Playing, nullptr))
        {
            std::erase(Voices, l_Voice);
            DestroyVoice(Engine, l_Voice);
        }
    }

    // Once the clip's header is in, and its samples, a clip that is not an AudioClip, or whose samples failed, ending it in silence
    void AudioScene::State::Load(Voice& voice)
    {
        if (voice.Loaded || voice.Ended)
        {
            return;
        }

        const AssetState l_State = voice.Clip.GetState();
        if (l_State == AssetState::Loading)
        {
            return;
        }

        const Asset* l_Asset = l_State == AssetState::Ready ? AssetManager::GetAsset(voice.Resource) : nullptr;
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

    // From where its time has got to. A stream is mixed once its first page is in, which offline is at once, so a render gives the same samples each time
    void AudioScene::State::MakeReal(Voice& voice)
    {
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

        if (ma_sound_init_from_data_source(&AudioEngineInternal::GetEngine(*Engine), l_Real.Source.GetDataSource(), 0, nullptr, &l_Real.Sound) != MA_SUCCESS)
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

    // Into the mixer's group its source names, or Master, through its occlusion node once it has one, and again whenever either changes. A new sound waits in the engine's output until then, not yet started
    void AudioScene::State::RouteVoice(Voice& voice)
    {
        RealVoice& l_Real = *voice.Real;
        ma_node* l_Node = l_Real.HasOcclusion ? static_cast<ma_node*>(&l_Real.Occlusion.Base) : static_cast<ma_node*>(&l_Real.Sound);
        if (l_Node == l_Real.Routed && voice.Values.Output == l_Real.RoutedGroup)
        {
            return;
        }

        if (l_Real.Routed != nullptr && l_Real.Routed != l_Node)
        {
            AudioEngineInternal::Unroute(*Engine, l_Real.Routed);
        }

        AudioEngineInternal::Route(*Engine, l_Node, voice.Values.Output);
        l_Real.Routed = l_Node;
        l_Real.RoutedGroup = voice.Values.Output;
    }

    // A real voice's time is its sound's, and a virtual voice's moves on at its clip's rate and its pitch
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

            voice.Ended = ma_sound_at_end(&voice.Real->Sound) == MA_TRUE;

            return;
        }

        if (voice.Paused || voice.Real != nullptr)
        {
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
        l_State.UpdateListener(l_Seconds);

        SceneRegistry& l_Registry = l_State.TheScene.GetRegistry();
        for (const auto [it_Handle, it_Source, it_ID, it_World] : l_Registry.view<AudioSourceComponent, IDComponent, WorldTransformComponent>().each())
        {
            const auto [a_Found, a_New] = l_State.Sources.try_emplace(it_ID.ID);
            SourceState& l_Source = a_Found->second;
            l_Source.Seen = l_State.Frame;

            const glm::vec3 l_Position(it_World.Matrix[3]);
            const glm::vec3 l_Velocity = l_Source.Placed && l_Seconds > 0.0f ? (l_Position - l_Source.Position) / l_Seconds : glm::vec3(0.0f);
            l_Source.Position = l_Position;
            l_Source.Placed = true;

            if (a_New && it_Source.PlayOnAwake)
            {
                l_State.StartVoice(l_Source, it_ID.ID, it_Source);
            }

            Voice* l_Voice = l_Source.Playing;
            if (l_Voice == nullptr)
            {
                continue;
            }

            // A source given another clip stops, as Unity's does
            if (it_Source.Resource != l_Voice->Resource)
            {
                l_State.EndVoice(l_Source);

                continue;
            }

            l_Voice->Values = it_Source;
            l_Voice->Position = l_Position;
            l_Voice->Velocity = l_Velocity;
            const glm::vec3 l_Facing(it_World.Matrix[2]);
            l_Voice->Forward = glm::dot(l_Facing, l_Facing) > 0.0f ? -glm::normalize(l_Facing) : glm::vec3(0.0f, 0.0f, -1.0f);
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

            float l_Loudness = it_Voice->Values.Mute ? 0.0f : std::clamp(it_Voice->Values.Volume, 0.0f, 1.0f) * GetOcclusionGain(it_Voice->Values, it_Voice->Occlusion);
            if (it_Voice->Values.Spatialize && l_State.HasListener)
            {
                const glm::vec3 l_ToListener = l_State.ListenerPosition - it_Voice->Position;
                const float l_Distance = glm::length(l_ToListener);
                l_Loudness *= GetAudioRolloffGain(it_Voice->Values, l_Distance);
                if (l_Distance > 0.0f)
                {
                    l_Loudness *= GetConeGain(it_Voice->Values, it_Voice->Forward, l_ToListener / l_Distance);
                }
            }

            it_Voice->Loudness = l_Loudness;
        }

        // Paused, every voice stays as it is, and its time with it
        if (!m_Paused)
        {
            l_State.ChooseRealVoices();
            for (Voice* it_Voice : l_State.Voices)
            {
                l_State.Advance(*it_Voice, l_Seconds);
            }
        }

        l_State.RemoveEnded();
    }

    void AudioScene::Play(Entity entity)
    {
        if (!entity.IsValid() || !entity.Has<AudioSourceComponent>())
        {
            return;
        }

        State& l_State = *m_State;
        const auto [a_Found, a_New] = l_State.Sources.try_emplace(entity.GetUUID());
        if (a_New)
        {
            a_Found->second.Seen = l_State.Frame;
            a_Found->second.Position = glm::vec3(entity.Get<WorldTransformComponent>().Matrix[3]);
        }

        l_State.StartVoice(a_Found->second, entity.GetUUID(), entity.Get<AudioSourceComponent>());
    }

    void AudioScene::Stop(Entity entity)
    {
        if (!entity.IsValid())
        {
            return;
        }

        if (const auto a_Found = m_State->Sources.find(entity.GetUUID()); a_Found != m_State->Sources.end())
        {
            m_State->EndVoice(a_Found->second);
        }
    }

    // At once, so the sound stops this frame, and its time stays where it stopped
    void AudioScene::Pause(Entity entity)
    {
        const auto a_Found = entity.IsValid() ? m_State->Sources.find(entity.GetUUID()) : m_State->Sources.end();
        Voice* l_Voice = a_Found != m_State->Sources.end() ? a_Found->second.Playing : nullptr;
        if (l_Voice == nullptr || l_Voice->Paused)
        {
            return;
        }

        l_Voice->Paused = true;
        if (l_Voice->Real != nullptr && l_Voice->Real->HasSound)
        {
            ma_sound_stop(&l_Voice->Real->Sound);
        }
    }

    // A voice that kept its sound starts again at once, unless the whole scene is paused, and one that did not is mixed again as the voices are next chosen
    void AudioScene::UnPause(Entity entity)
    {
        const auto a_Found = entity.IsValid() ? m_State->Sources.find(entity.GetUUID()) : m_State->Sources.end();
        Voice* l_Voice = a_Found != m_State->Sources.end() ? a_Found->second.Playing : nullptr;
        if (l_Voice == nullptr || !l_Voice->Paused)
        {
            return;
        }

        l_Voice->Paused = false;
        if (!m_Paused && l_Voice->Real != nullptr && l_Voice->Real->HasSound)
        {
            ma_sound_start(&l_Voice->Real->Sound);
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
        const Voice* l_Voice = a_Found != m_State->Sources.end() ? a_Found->second.Playing : nullptr;
        if (l_Voice == nullptr || l_Voice->Storage == nullptr)
        {
            return 0.0;
        }

        double l_Cursor = l_Voice->Cursor;
        ma_uint64 l_Frames = 0;
        if (l_Voice->Real != nullptr && l_Voice->Real->HasSound && ma_sound_get_cursor_in_pcm_frames(&l_Voice->Real->Sound, &l_Frames) == MA_SUCCESS)
        {
            l_Cursor = static_cast<double>(l_Frames);
        }

        return l_Cursor / l_Voice->Storage->Header.SampleRate;
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
        for (Voice* it_Voice : m_State->Voices)
        {
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

    // The voices go on as they are, mixed or not, so a kept sound has no gap. The scenes share one engine
    AudioCarry AudioScene::Extract(std::span<const UUID> entities)
    {
        State& l_State = *m_State;
        AudioCarry l_Carry;
        l_Carry.m_State = Memory::New<AudioCarry::State>(MemoryTag::Audio);
        l_Carry.m_State->Engine = l_State.Engine;
        for (const UUID it_Entity : entities)
        {
            const auto a_Found = l_State.Sources.find(it_Entity);
            if (a_Found == l_State.Sources.end())
            {
                continue;
            }

            if (Voice* l_Voice = a_Found->second.Playing)
            {
                std::erase(l_State.Voices, l_Voice);
                l_Carry.m_State->Voices.push_back(l_Voice);
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
            if (!it_Voice->Paused && it_Voice->Real != nullptr && it_Voice->Real->HasSound)
            {
                ++l_Statistics.Real;
                l_Statistics.Streams += it_Voice->Real->Source.GetStream() != nullptr ? 1u : 0u;
            }
            else
            {
                ++l_Statistics.Virtual;
            }
        }

        return l_Statistics;
    }

    const AudioSettings& AudioScene::GetSettings() const
    {
        return m_State->Settings;
    }
}