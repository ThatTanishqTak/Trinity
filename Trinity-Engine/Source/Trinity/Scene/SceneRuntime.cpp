#include "Trinity/Scene/SceneRuntime.hpp"

#include "Trinity/Audio/AudioScene.hpp"
#include "Trinity/Core/ConsoleVariable.hpp"

#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Input/Input.hpp"
#include "Trinity/Input/InputActionsRuntime.hpp"
#include "Trinity/Physics/PhysicsScene2D.hpp"
#include "Trinity/Physics/PhysicsScene3D.hpp"
#include "Trinity/Physics/PhysicsWorld2D.hpp"
#include "Trinity/Physics/PhysicsWorld3D.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/Scripting/ScriptRuntime.hpp"
#include "Trinity/UI/GameUI.hpp"
#include "Trinity/UI/GameUIAccess.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace Trinity
{
    namespace
    {
        // Here rather than with the physics, whose sources are built for instructions this processor may lack and must run no code before the CPU check
        ConsoleVariable<std::int32_t> s_PhysicsEventsVariable("physics.events", 0, "Logs a running scene's physics events to the Console: 0 none, 1 contacts beginning and ending, triggers entered and left, joints breaking and characters hitting bodies, 2 contacts staying too");

        // A joint to the world names no entity
        std::string GetEntityName(Scene& scene, UUID id)
        {
            if (!id)
            {
                return "the world";
            }

            const Entity l_Entity = scene.FindEntityByUUID(id);

            return l_Entity ? std::string(l_Entity.Get<TagComponent>().Tag) : std::format("entity {}", id);
        }
    }

    SceneRuntime::SceneRuntime(Scene& scene, const PhysicsSettings& settings, AudioEngine* audio, const AudioSettings& audioSettings) : m_Scene(scene), m_Settings(settings), m_Clock(settings.StepRate), m_Audio(CreateScope<AudioScene>(scene, audio, audioSettings))
    {
        if (PhysicsScene3D::IsNeeded(scene))
        {
            m_Physics = CreateScope<PhysicsScene3D>(scene, settings.Layers);
            m_Physics->GetWorld().SetGravity(settings.Gravity3D);
        }

        if (PhysicsScene2D::IsNeeded(scene))
        {
            m_Physics2D = CreateScope<PhysicsScene2D>(scene, settings.Layers);
            m_Physics2D->GetWorld().SetGravity(settings.Gravity2D);
        }

        // The scripts' fixed step is the first system, ahead of physics
        m_Scripts = CreateScope<ScriptRuntime>(scene);
        m_Scripts->SetPhysics(m_Physics.get(), m_Physics2D.get());
        m_Scripts->SetAudio(m_Audio.get());
        AddFixedStepSystem([this](Timestep step) { m_Scripts->FixedUpdate(step); });

        // Sounds asked for before the scene ran are not its
        const GameUI* l_UI = GameUIAccess::GetUI();
        m_UISoundCursor = l_UI != nullptr ? l_UI->GetSoundSequence() : 0;
    }

    // The scripts go first, with OnDestroy, while the physics and the scene are whole
    SceneRuntime::~SceneRuntime()
    {
        m_Scripts.reset();
    }

    void SceneRuntime::AddFixedStepSystem(FixedStepSystem system)
    {
        m_Systems.push_back(std::move(system));
    }

    // The clock goes on by the frame's time scaled by the scripts' time scale, so at 0 it takes no step
    void SceneRuntime::Update(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();

        const float l_TimeScale = m_Scripts->GetTime().TimeScale;
        const Timestep l_Scaled = l_TimeScale == 1.0f ? timestep : Timestep(std::chrono::nanoseconds(std::llround(static_cast<double>(timestep.GetDuration().count()) * static_cast<double>(l_TimeScale))));
        const std::uint32_t l_Steps = m_Clock.Advance(l_Scaled.GetDuration());
        RunSteps(l_Steps, m_Clock.GetInterpolation(), timestep, l_Scaled);
    }

    void SceneRuntime::Step()
    {
        TR_PROFILE_FUNCTION();

        m_Clock.TakeStep();
        RunSteps(1, 1.0f, Timestep(m_Clock.GetStep()), Timestep(m_Clock.GetStep()));
    }

    // The actions are read from Input first, so every script sees the same frame of them, and all through the frame Input reads as the scene has been let have it, as Forge's Game panel lets it. The scripts' clock moves on, scripts added since the last frame are made, and C# scripts' action handlers are raised, then the sounds the UI's elements asked for since the last frame play, and the UI events queued since then go to their handlers, then each step's systems, the scripts' fixed step first, then physics and the step's events to the scripts, with the presses each step reads cleared after it. Then the scripts' OnUpdate, the transform pass with moving bodies placed by the interpolation given, OnLateUpdate and, when any script had it, the transform pass again. Scripts added meanwhile that no call has given OnCreate, such as those of a Script component put on an entity directly, have it then, with the transform pass again, so it comes in the frame they were added in. Entities destroyed meanwhile go, then the sounds follow where everything now is, muffled by what the physics has between them and the listener, and the voices of sources gone stop, then rendering, which the caller does
    void SceneRuntime::RunSteps(std::uint32_t steps, float interpolation, Timestep unscaled, Timestep timestep)
    {
        const bool l_WasReadingScene = Input::SetReadingScene(true);
        InputActionsRuntime::Update(unscaled);
        m_Scripts->BeginFrame(unscaled, timestep, Timestep(m_Clock.GetStep()));
        static_cast<void>(m_Scripts->CreatePending());
        m_Scripts->RaiseInputEvents();
        PlayUISounds();
        m_Scripts->RaiseUIEvents();

        // A scene that had no 3D or no 2D body as it started gets that world once it has one
        if (!m_Physics && PhysicsScene3D::IsNeeded(m_Scene))
        {
            m_Physics = CreateScope<PhysicsScene3D>(m_Scene, m_Settings.Layers);
            m_Physics->GetWorld().SetGravity(m_Settings.Gravity3D);
            m_Physics->SetGatherContacts(m_GatherContacts);
        }

        if (!m_Physics2D && PhysicsScene2D::IsNeeded(m_Scene))
        {
            m_Physics2D = CreateScope<PhysicsScene2D>(m_Scene, m_Settings.Layers);
            m_Physics2D->GetWorld().SetGravity(m_Settings.Gravity2D);
            m_Physics2D->SetGatherContacts(m_GatherContacts);
        }

        m_Scripts->SetPhysics(m_Physics.get(), m_Physics2D.get());
        m_Audio->SetPhysics(m_Physics.get(), m_Physics2D.get());

        const auto l_Start = std::chrono::steady_clock::now();
        m_StepsThisFrame = steps;
        const Timestep l_Step(m_Clock.GetStep());
        if (m_Physics)
        {
            m_Physics->BeginFrame(m_StepsThisFrame);
        }

        if (m_Physics2D)
        {
            m_Physics2D->BeginFrame(m_StepsThisFrame);
        }

        // Events raised as a frame begins, such as the contacts of a body taken out ending, go to the scripts with the first step's
        std::size_t l_Delivered3D = 0;
        std::size_t l_Delivered2D = 0;
        for (std::uint32_t it_Step = 0; it_Step < m_StepsThisFrame; ++it_Step)
        {
            TR_PROFILE_SCOPE("SceneRuntime::Step");
            InputActionsRuntime::BeginFixedStep();
            for (const FixedStepSystem& it_System : m_Systems)
            {
                it_System(l_Step);
            }

            if (m_Physics)
            {
                m_Physics->Step(l_Step, it_Step);
                m_Scripts->DeliverEvents(m_Physics->GetEvents().subspan(l_Delivered3D));
                l_Delivered3D = m_Physics->GetEvents().size();
            }

            if (m_Physics2D)
            {
                m_Physics2D->Step(l_Step, it_Step);
                m_Scripts->DeliverEvents(m_Physics2D->GetEvents().subspan(l_Delivered2D));
                l_Delivered2D = m_Physics2D->GetEvents().size();
            }

            InputActionsRuntime::EndFixedStep();
        }

        if (m_Physics)
        {
            m_Physics->EndFrame(interpolation);
        }

        if (m_Physics2D)
        {
            m_Physics2D->EndFrame(interpolation);
        }

        m_StepMilliseconds = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - l_Start).count();

        LogPhysicsEvents();

        m_Scripts->Update(timestep);
        m_Scene.UpdateWorldTransforms();
        if (m_Scripts->LateUpdate(timestep))
        {
            m_Scene.UpdateWorldTransforms();
        }

        if (m_Scripts->CreatePending())
        {
            m_Scene.UpdateWorldTransforms();
        }

        m_Scripts->DestroyPending();
        m_Audio->Update(unscaled);
        Input::SetReadingScene(l_WasReadingScene);
    }

    SceneRuntime::Statistics SceneRuntime::GetStatistics() const
    {
        Statistics l_Statistics;
        l_Statistics.Steps = m_StepsThisFrame;
        l_Statistics.StepMilliseconds = m_StepMilliseconds;
        if (m_Physics)
        {
            l_Statistics.Bodies3D = m_Physics->GetBodyCounts();
            l_Statistics.Characters3D = m_Physics->GetCharacterCounts();
        }

        if (m_Physics2D)
        {
            l_Statistics.Bodies2D = m_Physics2D->GetBodyCounts();
            l_Statistics.Characters2D = m_Physics2D->GetCharacterCounts();
        }

        return l_Statistics;
    }

    void SceneRuntime::SetGatherContacts(bool gather)
    {
        m_GatherContacts = gather;
        if (m_Physics)
        {
            m_Physics->SetGatherContacts(gather);
        }

        if (m_Physics2D)
        {
            m_Physics2D->SetGatherContacts(gather);
        }
    }

    // Each sound the scene's UI asked for since the last frame, in order, through its UIDocument's Sound Output, or with the UIDocument gone, as though it named none
    void SceneRuntime::PlayUISounds()
    {
        const GameUI* l_UI = GameUIAccess::GetUI();
        if (l_UI == nullptr || l_UI->GetSoundSequence() == m_UISoundCursor)
        {
            return;
        }

        std::size_t l_Count = 0;
        {
            const MemoryTagScope l_Scope(MemoryTag::Scene);
            l_Count = l_UI->GetSounds(m_Scene, m_UISoundCursor, m_UISounds);
        }

        m_UISoundCursor = l_UI->GetSoundSequence();
        for (std::size_t it_Sound = 0; it_Sound < l_Count; ++it_Sound)
        {
            const Entity l_Entity = m_Scene.FindEntityByUUID(m_UISounds[it_Sound].Entity);
            const UUID l_Output = l_Entity && l_Entity.Has<UIDocumentComponent>() ? l_Entity.Get<UIDocumentComponent>().SoundOutput : UUID();
            static_cast<void>(m_Audio->PlayUI(m_UISounds[it_Sound].Resource, l_Output));
        }
    }

    // Numbered by the step they came from since the scene started, the 3D world's then the 2D world's
    void SceneRuntime::LogPhysicsEvents() const
    {
        const std::int32_t l_Level = s_PhysicsEventsVariable.Get();
        if (l_Level <= 0)
        {
            return;
        }

        const std::uint64_t l_FirstStep = m_Clock.GetStepCount() - m_StepsThisFrame + 1;
        if (m_Physics)
        {
            for (const PhysicsEvent3D& it_Event : m_Physics->GetEvents())
            {
                if (it_Event.Type == PhysicsEventType::ContactStay && l_Level < 2)
                {
                    continue;
                }

                const std::string l_Where = it_Event.Type == PhysicsEventType::ContactBegin || it_Event.Type == PhysicsEventType::CharacterHit ? std::format(" at ({:.3f}, {:.3f}, {:.3f}) along ({:.3f}, {:.3f}, {:.3f})", it_Event.Point.x, it_Event.Point.y, it_Event.Point.z, it_Event.Normal.x, it_Event.Normal.y, it_Event.Normal.z) : std::string();
                TR_CORE_INFO("Physics: step {}, {}: {} and {}{}", l_FirstStep + it_Event.Step, ToString(it_Event.Type), GetEntityName(m_Scene, it_Event.First), GetEntityName(m_Scene, it_Event.Second), l_Where);
            }
        }

        if (m_Physics2D)
        {
            for (const PhysicsEvent2D& it_Event : m_Physics2D->GetEvents())
            {
                if (it_Event.Type == PhysicsEventType::ContactStay && l_Level < 2)
                {
                    continue;
                }

                const std::string l_Where = it_Event.Type == PhysicsEventType::ContactBegin || it_Event.Type == PhysicsEventType::CharacterHit ? std::format(" at ({:.3f}, {:.3f}) along ({:.3f}, {:.3f})", it_Event.Point.x, it_Event.Point.y, it_Event.Normal.x, it_Event.Normal.y) : std::string();
                TR_CORE_INFO("Physics: step {}, 2D {}: {} and {}{}", l_FirstStep + it_Event.Step, ToString(it_Event.Type), GetEntityName(m_Scene, it_Event.First), GetEntityName(m_Scene, it_Event.Second), l_Where);
            }
        }
    }
}