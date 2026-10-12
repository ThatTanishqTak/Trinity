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
    class ScriptRuntime;

    class TRINITY_API SceneRuntime
    {
    public:
        // Called once a step, with the step's length
        using FixedStepSystem = std::function<void(Timestep step)>;

        // The last frame's steps and the time they took, systems, scripts' fixed steps and physics together, and each world's bodies as it reports them
        struct Statistics
        {
            std::uint32_t Steps = 0;
            float StepMilliseconds = 0.0f;
            PhysicsBodyCounts Bodies3D;
            PhysicsBodyCounts Bodies2D;
        };

        explicit SceneRuntime(Scene& scene, const PhysicsSettings& settings = {});
        ~SceneRuntime();

        SceneRuntime(const SceneRuntime&) = delete;
        SceneRuntime& operator=(const SceneRuntime&) = delete;

        void AddFixedStepSystem(FixedStepSystem system);

        // With the frame's time, as Application hands it to each layer
        void Update(Timestep timestep);
        // One fixed step whatever the frame's time, as a paused scene's Step button takes, with moving bodies drawn where it left them
        void Step();

        [[nodiscard]] Scene& GetScene() { return m_Scene; }
        [[nodiscard]] SimulationClock& GetClock() { return m_Clock; }
        [[nodiscard]] const SimulationClock& GetClock() const { return m_Clock; }
        [[nodiscard]] float GetInterpolation() const { return m_Clock.GetInterpolation(); }
        [[nodiscard]] std::uint32_t GetStepsThisFrame() const { return m_StepsThisFrame; }
        // The bodies are counted as asked, which in 3D walks every body
        [[nodiscard]] Statistics GetStatistics() const;
        // Whether both worlds keep where their bodies touch each step, for drawing
        void SetGatherContacts(bool gather);
        // Nothing until the scene has a RigidBody or a collider, as it starts or later while it runs
        [[nodiscard]] PhysicsScene3D* GetPhysics() { return m_Physics.get(); }
        // Nothing until the scene has a RigidBody2D or a 2D collider, as it starts or later while it runs
        [[nodiscard]] PhysicsScene2D* GetPhysics2D() { return m_Physics2D.get(); }
        [[nodiscard]] ScriptRuntime& GetScripts() { return *m_Scripts; }

    private:
        void RunSteps(std::uint32_t steps, float interpolation, Timestep timestep);
        void LogPhysicsEvents() const;

        Scene& m_Scene;
        PhysicsSettings m_Settings;
        SimulationClock m_Clock;
        Scope<PhysicsScene3D> m_Physics;
        Scope<PhysicsScene2D> m_Physics2D;
        std::vector<FixedStepSystem, TaggedAllocator<FixedStepSystem, MemoryTag::Scene>> m_Systems;
        // After the physics, so the scripts' OnDestroy runs while the bodies still exist
        Scope<ScriptRuntime> m_Scripts;
        std::uint32_t m_StepsThisFrame = 0;
        float m_StepMilliseconds = 0.0f;
        bool m_GatherContacts = false;
    };
}