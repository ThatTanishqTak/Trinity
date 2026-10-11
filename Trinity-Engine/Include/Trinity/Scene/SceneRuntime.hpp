#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Scene/SimulationClock.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace Trinity
{
    class PhysicsScene2D;
    class PhysicsScene3D;
    class Scene;

    // Runs a scene over time, on the main thread. Each frame takes the fixed steps its clock asks for, each running every fixed-step system in the order they were added and then physics, 3D then 2D, then the transform pass, after which the scene is drawn with its moving bodies between their last two steps. Scripts add their systems, ahead of physics, in M7. The rate is physics.rate, and the scene's 3D and 2D physics are made from its components with the project's collision layers, when the runtime is made. A runtime does not own its scene, which must outlive it
    class TRINITY_API SceneRuntime
    {
    public:
        // Called once a step, with the step's length
        using FixedStepSystem = std::function<void(Timestep step)>;

        explicit SceneRuntime(Scene& scene, const CollisionLayers& layers = {});
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
        // Nothing when the scene had no RigidBody2D and no 2D collider as it started
        [[nodiscard]] PhysicsScene2D* GetPhysics2D() { return m_Physics2D.get(); }

    private:
        void LogPhysicsEvents() const;

        Scene& m_Scene;
        SimulationClock m_Clock;
        Scope<PhysicsScene3D> m_Physics;
        Scope<PhysicsScene2D> m_Physics2D;
        std::vector<FixedStepSystem, TaggedAllocator<FixedStepSystem, MemoryTag::Scene>> m_Systems;
        std::uint32_t m_StepsThisFrame = 0;
    };
}