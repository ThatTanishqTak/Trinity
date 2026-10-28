#include "Trinity/Scripting/Interop.hpp"

#include "Trinity/Audio/AudioMixer.hpp"
#include "Trinity/Audio/AudioMixerData.hpp"
#include "Trinity/Audio/AudioScene.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scripting/ScriptApi.hpp"
#include "Trinity/Scripting/ScriptRuntime.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Trinity::Interop
{
    namespace
    {
        // The running scene's sounds, or null while no scene runs
        [[nodiscard]] AudioScene* GetAudio()
        {
            ScriptRuntime* l_Runtime = ScriptRuntime::GetActive();

            return l_Runtime != nullptr ? l_Runtime->GetAudio() : nullptr;
        }

        // The call on the entity's source, NoEntity for an entity the running scene does not have and NoComponent for one with no AudioSource. Done with nothing called while the scene has no sounds, as one made without them
        template<typename F>
        [[nodiscard]] Int32 OnSource(EntityId entity, F&& call)
        {
            const Entity l_Entity = FindEntity(entity);
            if (!l_Entity)
            {
                return std::to_underlying(AccessResult::NoEntity);
            }

            if (!l_Entity.Has<AudioSourceComponent>())
            {
                return std::to_underlying(AccessResult::NoComponent);
            }

            if (AudioScene* l_Audio = GetAudio())
            {
                std::forward<F>(call)(*l_Audio, l_Entity);
            }

            return std::to_underlying(AccessResult::Done);
        }

        // The project's mixer, for the project's asset or none. Another is refused, with one warning until yet another is, so a script that asks every frame warns once
        [[nodiscard]] AudioMixer* FindMixer(UInt64 mixer)
        {
            AudioScene* l_Audio = GetAudio();
            if (l_Audio == nullptr)
            {
                return nullptr;
            }

            const UUID l_Project = l_Audio->GetSettings().Mixer;
            if (mixer != 0 && UUID(mixer) != l_Project)
            {
                static UUID s_Warned;
                if (std::exchange(s_Warned, UUID(mixer)) != UUID(mixer))
                {
                    TR_CORE_WARN("Scripting: nothing was done to mixer {}, since the project's mixer is {}, the only one its scenes play through", UUID(mixer), l_Project ? std::format("{}", l_Project) : std::string("none, so Master alone"));
                }

                return nullptr;
            }

            return l_Audio->GetMixer();
        }
    }

    Int32 AudioSource_Play(EntityId entity)
    {
        return OnSource(entity, [](AudioScene& audio, Entity source) { audio.Play(source); });
    }

    Int32 AudioSource_Stop(EntityId entity)
    {
        return OnSource(entity, [](AudioScene& audio, Entity source) { audio.Stop(source); });
    }

    Int32 AudioSource_Pause(EntityId entity)
    {
        return OnSource(entity, [](AudioScene& audio, Entity source) { audio.Pause(source); });
    }

    Int32 AudioSource_UnPause(EntityId entity)
    {
        return OnSource(entity, [](AudioScene& audio, Entity source) { audio.UnPause(source); });
    }

    Int32 AudioSource_IsPlaying(EntityId entity, Bool* playing)
    {
        *playing = false;

        return OnSource(entity, [playing](AudioScene& audio, Entity source) { *playing = audio.IsPlaying(source); });
    }

    Int32 AudioSource_GetTime(EntityId entity, Double* time)
    {
        *time = 0.0;

        return OnSource(entity, [time](AudioScene& audio, Entity source) { *time = audio.GetTime(source); });
    }

    Int32 AudioSource_PlayOneShot(EntityId entity, UInt64 resource, Float volumeScale, UInt64* handle)
    {
        *handle = 0;

        return OnSource(entity, [&](AudioScene& audio, Entity source) { *handle = audio.PlayOneShot(source, UUID(resource), volumeScale).ID; });
    }

    Int32 AudioSource_SetParameter(EntityId entity, Utf8 name, Float value)
    {
        return OnSource(entity, [&](AudioScene& audio, Entity source) { static_cast<void>(audio.SetParameter(source, ToStringView(name), value)); });
    }

    Int32 AudioSource_SetParameterLabel(EntityId entity, Utf8 name, Utf8 label, Bool* found)
    {
        *found = false;

        return OnSource(entity, [&](AudioScene& audio, Entity source) { *found = audio.SetParameterLabel(source, ToStringView(name), ToStringView(label)); });
    }

    Int32 AudioSource_GetParameter(EntityId entity, Utf8 name, Float* value, Bool* found)
    {
        *found = false;

        return OnSource(entity, [&](AudioScene& audio, Entity source)
        {
            if (const std::optional<float> l_Value = audio.GetParameter(source, ToStringView(name)))
            {
                *value = *l_Value;
                *found = true;
            }
        });
    }

    Int32 AudioSource_GetOutput(EntityId entity, UInt64* group)
    {
        *group = 0;
        const Entity l_Entity = FindEntity(entity);
        if (!l_Entity)
        {
            return std::to_underlying(AccessResult::NoEntity);
        }

        if (!l_Entity.Has<AudioSourceComponent>())
        {
            return std::to_underlying(AccessResult::NoComponent);
        }

        *group = l_Entity.Get<AudioSourceComponent>().Output.GetValue();

        return std::to_underlying(AccessResult::Done);
    }

    // Through patch, as a property is written, so the voice playing moves to the group as the scene's sounds next update. One the mixer lacks plays into Master
    Int32 AudioSource_SetOutput(EntityId entity, UInt64 group)
    {
        Entity l_Entity = FindEntity(entity);
        if (!l_Entity)
        {
            return std::to_underlying(AccessResult::NoEntity);
        }

        if (!l_Entity.Has<AudioSourceComponent>())
        {
            return std::to_underlying(AccessResult::NoComponent);
        }

        l_Entity.Patch<AudioSourceComponent>([group](AudioSourceComponent& component) { component.Output = UUID(group); });

        return std::to_underlying(AccessResult::Done);
    }

    UInt64 Audio_PlayAtPoint(UInt64 resource, const Vector3* position, Float volume)
    {
        AudioScene* l_Audio = GetAudio();

        return l_Audio != nullptr ? l_Audio->PlayAtPoint(UUID(resource), *position, volume).ID : 0;
    }

    UInt64 Audio_PlayAttached(UInt64 resource, EntityId entity, Float volume)
    {
        AudioScene* l_Audio = GetAudio();
        const Entity l_Entity = FindEntity(entity);

        return l_Audio != nullptr && l_Entity ? l_Audio->PlayAttached(UUID(resource), l_Entity, volume).ID : 0;
    }

    Bool Audio_SetGlobalParameter(Utf8 name, Float value)
    {
        AudioScene* l_Audio = GetAudio();

        return l_Audio != nullptr && l_Audio->SetGlobalParameter(ToStringView(name), value);
    }

    Bool Audio_GetGlobalParameter(Utf8 name, Float* value)
    {
        const AudioScene* l_Audio = GetAudio();
        const std::optional<float> l_Value = l_Audio != nullptr ? l_Audio->GetGlobalParameter(ToStringView(name)) : std::nullopt;
        if (l_Value)
        {
            *value = *l_Value;
        }

        return l_Value.has_value();
    }

    Void AudioHandle_Stop(UInt64 handle, Float fadeSeconds)
    {
        if (AudioScene* l_Audio = GetAudio())
        {
            l_Audio->Stop(AudioHandle{ handle }, fadeSeconds);
        }
    }

    Bool AudioHandle_IsPlaying(UInt64 handle)
    {
        const AudioScene* l_Audio = GetAudio();

        return l_Audio != nullptr && l_Audio->IsPlaying(AudioHandle{ handle });
    }

    Double AudioHandle_GetTime(UInt64 handle)
    {
        const AudioScene* l_Audio = GetAudio();

        return l_Audio != nullptr ? l_Audio->GetTime(AudioHandle{ handle }) : 0.0;
    }

    Bool AudioHandle_SetParameter(UInt64 handle, Utf8 name, Float value)
    {
        AudioScene* l_Audio = GetAudio();

        return l_Audio != nullptr && l_Audio->SetParameter(AudioHandle{ handle }, ToStringView(name), value);
    }

    Bool AudioHandle_SetParameterLabel(UInt64 handle, Utf8 name, Utf8 label)
    {
        AudioScene* l_Audio = GetAudio();

        return l_Audio != nullptr && l_Audio->SetParameterLabel(AudioHandle{ handle }, ToStringView(name), ToStringView(label));
    }

    Bool AudioHandle_GetParameter(UInt64 handle, Utf8 name, Float* value)
    {
        const AudioScene* l_Audio = GetAudio();
        const std::optional<float> l_Value = l_Audio != nullptr ? l_Audio->GetParameter(AudioHandle{ handle }, ToStringView(name)) : std::nullopt;
        if (l_Value)
        {
            *value = *l_Value;
        }

        return l_Value.has_value();
    }

    Void AudioHandle_SetPosition(UInt64 handle, const Vector3* position)
    {
        if (AudioScene* l_Audio = GetAudio())
        {
            l_Audio->SetPosition(AudioHandle{ handle }, *position);
        }
    }

    // Nothing for an entity the running scene does not have
    Void AudioHandle_Follow(UInt64 handle, EntityId entity)
    {
        AudioScene* l_Audio = GetAudio();
        const Entity l_Entity = FindEntity(entity);
        if (l_Audio != nullptr && l_Entity)
        {
            l_Audio->Follow(AudioHandle{ handle }, l_Entity);
        }
    }

    UInt64 AudioMixer_GetAsset()
    {
        const AudioScene* l_Audio = GetAudio();

        return l_Audio != nullptr ? l_Audio->GetSettings().Mixer.GetValue() : 0;
    }

    Bool AudioMixer_SetFloat(UInt64 mixer, Utf8 name, Float value)
    {
        AudioMixer* l_Mixer = FindMixer(mixer);

        return l_Mixer != nullptr && l_Mixer->SetFloat(ToStringView(name), value);
    }

    Bool AudioMixer_GetFloat(UInt64 mixer, Utf8 name, Float* value)
    {
        const AudioMixer* l_Mixer = FindMixer(mixer);
        const std::optional<float> l_Value = l_Mixer != nullptr ? l_Mixer->GetFloat(ToStringView(name)) : std::nullopt;
        if (l_Value)
        {
            *value = *l_Value;
        }

        return l_Value.has_value();
    }

    Bool AudioMixer_ClearFloat(UInt64 mixer, Utf8 name)
    {
        AudioMixer* l_Mixer = FindMixer(mixer);

        return l_Mixer != nullptr && l_Mixer->ClearFloat(ToStringView(name));
    }

    Bool AudioMixer_TransitionToSnapshot(UInt64 mixer, Utf8 name, Float seconds)
    {
        AudioMixer* l_Mixer = FindMixer(mixer);

        return l_Mixer != nullptr && l_Mixer->TransitionToSnapshot(ToStringView(name), seconds);
    }

    // The first by that name in the mixer's order, Master first
    UInt64 AudioMixer_FindGroup(UInt64 mixer, Utf8 name)
    {
        const AudioMixer* l_Mixer = FindMixer(mixer);
        if (l_Mixer == nullptr)
        {
            return 0;
        }

        const auto& l_Groups = l_Mixer->GetData().Groups;
        const auto a_Found = std::ranges::find(l_Groups, ToStringView(name), [](const AudioMixerGroup& group) { return std::string_view(group.Name); });

        return a_Found != l_Groups.end() ? a_Found->ID.GetValue() : 0;
    }
}