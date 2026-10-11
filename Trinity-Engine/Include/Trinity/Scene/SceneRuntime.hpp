#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Scene/SimulationClock.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace Trinity
{
    class PhysicsScene3D;
    class Scene;

    // Runs a scene over time, on the main thread. Each frame takes the fixed steps its clock asks for, each running every fixed-step system in the order they were added and then physics, then the transform pass, after which the scene is drawn with its moving bodies between their last two steps. Scripts add their systems, ahead of physics, in M7. The rate is physics.rate, and the scene's physics is made from its components, when the runtime is made. A runtime does not own its scene, which must outlive it
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
        // Nothing when the scene had no RigidBody and no collider as it started
        [[nodiscard]] PhysicsScene3D* GetPhysics() { return m_Physics.get(); }

    private:
        void LogPhysicsEvents() const;

        Scene& m_Scene;
        SimulationClock m_Clock;
        Scope<PhysicsScene3D> m_Physics;
        std::vector<FixedStepSystem, TaggedAllocator<FixedStepSystem, MemoryTag::Scene>> m_Systems;
        std::uint32_t m_StepsThisFrame = 0;
    };
}