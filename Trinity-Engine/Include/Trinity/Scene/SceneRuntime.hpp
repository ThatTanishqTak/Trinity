#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Scene/SimulationClock.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace Trinity
{
    class Scene;

    // Runs a scene over time, on the main thread. Each frame takes the fixed steps its clock asks for, each running every fixed-step system in the order they were added, then the transform pass, after which the scene is drawn, interpolated by GetInterpolation between its last two steps. Physics adds its systems in M6, and scripts theirs, ahead of physics, in M7. The rate is physics.rate when the runtime is made. A runtime does not own its scene, which must outlive it
    class TRINITY_API SceneRuntime
    {
    public:
        // Called once a step, with the step's length
        using FixedStepSystem = std::function<void(Timestep step)>;

        explicit SceneRuntime(Scene& scene);
        ~SceneRuntime();

        SceneRuntime(const SceneRuntime&) = delete;
        SceneRuntime& operator=(const SceneRuntime&) = delete;

        void AddFixedStepSystem(FixedStepSystem system);

        // With the frame's time, as Application hands it to each layer
        void Update(Timestep timestep);

        [[nodiscard]] Scene& GetScene() { return m_Scene; }
        [[nodiscard]] SimulationClock& GetClock() { return m_Clock; }
        [[nodiscard]] const SimulationClock& GetClock() const { return m_Clock; }
        [[nodiscard]] float GetInterpolation() const { return m_Clock.GetInterpolation(); }
        [[nodiscard]] std::uint32_t GetStepsThisFrame() const { return m_StepsThisFrame; }

    private:
        Scene& m_Scene;
        SimulationClock m_Clock;
        std::vector<FixedStepSystem, TaggedAllocator<FixedStepSystem, MemoryTag::Scene>> m_Systems;
        std::uint32_t m_StepsThisFrame = 0;
    };
}