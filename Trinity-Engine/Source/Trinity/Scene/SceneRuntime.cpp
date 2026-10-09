#include "Trinity/Scene/SceneRuntime.hpp"

#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <algorithm>
#include <utility>

namespace Trinity
{
    namespace
    {
        ConsoleVariable<std::int32_t> s_PhysicsRateVariable("physics.rate", static_cast<std::int32_t>(SimulationClock::c_DefaultRate), "Fixed simulation steps per second for a running scene, from 1 to 1000, taken when the scene starts running");
    }

    SceneRuntime::SceneRuntime(Scene& scene) : m_Scene(scene), m_Clock(static_cast<std::uint32_t>(std::max(s_PhysicsRateVariable.Get(), 1)))
    {

    }

    SceneRuntime::~SceneRuntime() = default;

    void SceneRuntime::AddFixedStepSystem(FixedStepSystem system)
    {
        m_Systems.push_back(std::move(system));
    }

    // Physics, then the transform pass, then rendering, which the caller does with the interpolation the clock leaves
    void SceneRuntime::Update(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();

        m_StepsThisFrame = m_Clock.Advance(timestep.GetDuration());
        const Timestep l_Step(m_Clock.GetStep());
        for (std::uint32_t it_Step = 0; it_Step < m_StepsThisFrame; ++it_Step)
        {
            TR_PROFILE_SCOPE("SceneRuntime::Step");
            for (const FixedStepSystem& it_System : m_Systems)
            {
                it_System(l_Step);
            }
        }

        m_Scene.UpdateWorldTransforms();
    }
}