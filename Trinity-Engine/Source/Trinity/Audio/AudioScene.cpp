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
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <miniaudio.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <string>
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

        // The part of a voice that is mixed: its clip read through a voice of its own, and the sound miniaudio mixes that in. Only a real voice has one, since a virtual voice needs only its time
        struct RealVoice
        {
            AudioClipVoice Source;
            ma_sound Sound{};
            bool HasSound = false;
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
        };

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

            if (l_Real->HasSound)
            {
                ma_uint64 l_Cursor = 0;
                if (ma_sound_get_cursor_in_pcm_frames(&l_Real->Sound, &l_Cursor) == MA_SUCCESS)
                {
                    voice.Cursor = static_cast<double>(l_Cursor);
                }

                ma_sound_uninit(&l_Real->Sound);
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
        void StartVoice(SourceState& source, UUID entity, const AudioSourceComponent& values);
        void EndVoice(SourceState& source);
        void Load(Voice& voice);
        void ChooseRealVoices();
        void MakeReal(Voice& voice);
        void StartSound(Voice& voice);
        void Advance(Voice& voice, float seconds);
        void RemoveEnded();

        Scene& TheScene;
        AudioEngine* Engine = nullptr;
        AudioSettings Settings;
        SourceMap Sources;
        // Every voice playing, ordered each frame by how much it matters
        VoiceList Voices;
        std::uint64_t Frame = 0;
        std::uint64_t NextNumber = 0;

        bool HasListener = false;
        bool ListenerPlaced = false;
        UUID Listener;
        glm::vec3 ListenerPosition{ 0.0f };
        std::size_t ListenerCount = 0;
        bool WarnedNoListener = false;
        bool WarnedVoices = false;
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

    // The most important first, then the loudest, then the first to play. Beyond the virtual voices the least of them stop, and with no listener or no engine none is mixed
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

            const bool l_Mixed = Engine != nullptr && HasListener && it_Voice->Loaded && !it_Voice->Paused && !it_Voice->Ended && l_Real < Settings.RealVoices;
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
            ApplyValues(voice, Settings);

            return;
        }

        if (AudioStream* l_Stream = l_Real.Source.GetStream(); l_Stream != nullptr && !l_Stream->IsPrimed())
        {
            return;
        }

        if (ma_sound_init_from_data_source(&AudioEngineInternal::GetEngine(*Engine), l_Real.Source.GetDataSource(), 0, &AudioEngineInternal::GetMaster(*Engine), &l_Real.Sound) != MA_SUCCESS)
        {
            TR_CORE_ERROR("Audio: the sound of entity {} could not start", voice.Entity);
            voice.Ended = true;

            return;
        }

        l_Real.HasSound = true;
        ApplyValues(voice, Settings);
        ma_sound_start(&l_Real.Sound);
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

    // Sources are read before the listener's move is known to their voices, so a source and its listener moving together read as still
    void AudioScene::Update(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's audio updates on the main thread.");

        State& l_State = *m_State;
        const float l_Seconds = std::max(timestep.GetSeconds(), 0.0f);
        ++l_State.Frame;
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

        for (Voice* it_Voice : l_State.Voices)
        {
            l_State.Load(*it_Voice);

            float l_Loudness = it_Voice->Values.Mute ? 0.0f : std::clamp(it_Voice->Values.Volume, 0.0f, 1.0f);
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