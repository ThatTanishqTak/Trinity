#include "Trinity/Scene/SceneRuntime.hpp"

#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Physics/PhysicsScene2D.hpp"
#include "Trinity/Physics/PhysicsScene3D.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <utility>

namespace Trinity
{
    namespace
    {
        ConsoleVariable<std::int32_t> s_PhysicsRateVariable("physics.rate", static_cast<std::int32_t>(SimulationClock::c_DefaultRate), "Fixed simulation steps per second for a running scene, from 1 to 1000, taken when the scene starts running");
        // Here rather than with the physics, whose sources are built for instructions this processor may lack and must run no code before the CPU check
        ConsoleVariable<std::int32_t> s_PhysicsEventsVariable("physics.events", 0, "Logs a running scene's physics events to the Console: 0 none, 1 contacts beginning and ending, triggers entered and left and joints breaking, 2 contacts staying too");

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

    SceneRuntime::SceneRuntime(Scene& scene, const CollisionLayers& layers) : m_Scene(scene), m_Clock(static_cast<std::uint32_t>(std::max(s_PhysicsRateVariable.Get(), 1)))
    {
        if (PhysicsScene3D::IsNeeded(scene))
        {
            m_Physics = CreateScope<PhysicsScene3D>(scene, layers);
        }

        if (PhysicsScene2D::IsNeeded(scene))
        {
            m_Physics2D = CreateScope<PhysicsScene2D>(scene, layers);
        }
    }

    SceneRuntime::~SceneRuntime() = default;

    void SceneRuntime::AddFixedStepSystem(FixedStepSystem system)
    {
        m_Systems.push_back(std::move(system));
    }

    void SceneRuntime::Update(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();

        const std::uint32_t l_Steps = m_Clock.Advance(timestep.GetDuration());
        RunSteps(l_Steps, m_Clock.GetInterpolation());
    }

    void SceneRuntime::Step()
    {
        TR_PROFILE_FUNCTION();

        m_Clock.TakeStep();
        RunSteps(1, 1.0f);
    }

    // Each step's systems then physics, then the transform pass with moving bodies placed by the interpolation given, then rendering, which the caller does
    void SceneRuntime::RunSteps(std::uint32_t steps, float interpolation)
    {
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

        for (std::uint32_t it_Step = 0; it_Step < m_StepsThisFrame; ++it_Step)
        {
            TR_PROFILE_SCOPE("SceneRuntime::Step");
            for (const FixedStepSystem& it_System : m_Systems)
            {
                it_System(l_Step);
            }

            if (m_Physics)
            {
                m_Physics->Step(l_Step, it_Step);
            }

            if (m_Physics2D)
            {
                m_Physics2D->Step(l_Step, it_Step);
            }
        }

        if (m_Physics)
        {
            m_Physics->EndFrame(interpolation);
        }

        if (m_Physics2D)
        {
            m_Physics2D->EndFrame(interpolation);
        }

        LogPhysicsEvents();

        m_Scene.UpdateWorldTransforms();
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

                const std::string l_Where = it_Event.Type == PhysicsEventType::ContactBegin ? std::format(" at ({:.3f}, {:.3f}, {:.3f}) along ({:.3f}, {:.3f}, {:.3f})", it_Event.Point.x, it_Event.Point.y, it_Event.Point.z, it_Event.Normal.x, it_Event.Normal.y, it_Event.Normal.z) : std::string();
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

                const std::string l_Where = it_Event.Type == PhysicsEventType::ContactBegin ? std::format(" at ({:.3f}, {:.3f}) along ({:.3f}, {:.3f})", it_Event.Point.x, it_Event.Point.y, it_Event.Normal.x, it_Event.Normal.y) : std::string();
                TR_CORE_INFO("Physics: step {}, 2D {}: {} and {}{}", l_FirstStep + it_Event.Step, ToString(it_Event.Type), GetEntityName(m_Scene, it_Event.First), GetEntityName(m_Scene, it_Event.Second), l_Where);
            }
        }
    }
}