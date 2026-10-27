#include "SoundEventAudition.hpp"

#include <algorithm>
#include <utility>

namespace
{
    // In front of the listener by less than any event's nearest distance, so the event is heard at its full volume, from ahead
    constexpr glm::vec3 c_AuditionPoint{ 0.0f, 0.0f, -0.01f };
}

SoundEventAudition::~SoundEventAudition()
{
    Clear();
}

void SoundEventAudition::Play(Trinity::UUID event)
{
    Stop();
    m_Event = event;
    m_Requested = static_cast<bool>(event);
}

void SoundEventAudition::Stop(float fadeSeconds)
{
    m_Requested = false;
    if (m_Audio && m_Handle)
    {
        m_Audio->Stop(m_Handle, fadeSeconds);
    }
}

// The scene goes before the engine it plays on is let go, and its sound with it. A play asked for starts with every value given to its parameters, a global one's already the engine's
void SoundEventAudition::Update(Trinity::AudioEngine* engine, Trinity::Timestep timestep)
{
    if (engine != m_Engine)
    {
        m_Handle = {};
        m_Audio.reset();
        m_Scene.reset();
        m_Engine = engine;
    }

    if (m_Engine == nullptr)
    {
        return;
    }

    if (!m_Audio)
    {
        m_Scene = std::make_unique<Trinity::Scene>();
        m_Scene->CreateEntity("Audition Listener").Add<Trinity::AudioListenerComponent>();
        m_Audio = std::make_unique<Trinity::AudioScene>(*m_Scene, m_Engine);
        for (const Value& it_Value : m_Values)
        {
            if (it_Value.Global)
            {
                m_Engine->SetGlobalParameter(it_Value.Name, it_Value.Number);
            }
        }
    }

    if (std::exchange(m_Requested, false))
    {
        m_Handle = m_Audio->PlayAtPoint(m_Event, c_AuditionPoint);
        for (const Value& it_Value : m_Values)
        {
            if (!it_Value.Global)
            {
                static_cast<void>(m_Audio->SetParameter(m_Handle, it_Value.Name, it_Value.Number));
            }
        }
    }

    m_Scene->UpdateWorldTransforms();
    m_Audio->Update(timestep);
}

void SoundEventAudition::Clear()
{
    m_Requested = false;
    m_Handle = {};
    m_Audio.reset();
    m_Scene.reset();
    m_Engine = nullptr;
    m_Event = {};
    m_Values.clear();
}

bool SoundEventAudition::IsPlaying(Trinity::UUID event) const
{
    return event && event == m_Event && (m_Requested || (m_Audio && m_Audio->IsPlaying(m_Handle)));
}

void SoundEventAudition::SetParameter(std::string_view name, float value, bool global)
{
    const auto a_Found = std::ranges::find(m_Values, name, &Value::Name);
    if (a_Found != m_Values.end())
    {
        a_Found->Number = value;
        a_Found->Global = global;
    }
    else
    {
        m_Values.push_back({ std::string(name), value, global });
    }

    if (global && m_Engine != nullptr)
    {
        m_Engine->SetGlobalParameter(name, value);
    }
    else if (!global && m_Audio && m_Handle)
    {
        static_cast<void>(m_Audio->SetParameter(m_Handle, name, value));
    }
}

std::optional<float> SoundEventAudition::GetParameter(std::string_view name) const
{
    const auto a_Found = std::ranges::find(m_Values, name, &Value::Name);

    return a_Found != m_Values.end() ? std::optional(a_Found->Number) : std::nullopt;
}

std::optional<float> SoundEventAudition::GetGlobalParameter(std::string_view name) const
{
    return m_Engine != nullptr ? m_Engine->GetGlobalParameter(name) : std::nullopt;
}

std::optional<float> SoundEventAudition::GetHeardParameter(std::string_view name) const
{
    return m_Audio && m_Handle ? m_Audio->GetHeardParameter(m_Handle, name) : std::nullopt;
}

void SoundEventAudition::ClearParameters()
{
    m_Values.clear();
}