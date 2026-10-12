#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Physics/PhysicsScene2D.hpp"
#include "Trinity/Physics/PhysicsScene3D.hpp"

#include <cstdint>
#include <span>

namespace Trinity
{
    class Entity;
    class Scene;

    // Every script instance of a running scene, made from its Script components through each language's backend and called in a fixed order: by when each script was added, which for the scene's own is hierarchy order, and in list order on each entity. A script whose class no loaded code has is kept in its component and does not run, with one warning a class. SceneRuntime owns one and drives it. Main thread only
    class TRINITY_API ScriptRuntime
    {
    public:
        struct Statistics
        {
            std::uint32_t Instances = 0;
            // Scripts that do not run, since no loaded code has their class
            std::uint32_t Unknown = 0;
        };

        explicit ScriptRuntime(Scene& scene);
        // OnDestroy on every script that had OnCreate, in the order they were made
        ~ScriptRuntime();

        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;

        // Makes the instances of every Script component added since the last call, the scene's own on the first, and calls OnCreate on each enabled one. A script that a call makes is made in the same call
        void CreatePending();
        void FixedUpdate(Timestep step);
        // A step's events, in the order they happened, to every script on both entities
        void DeliverEvents(std::span<const PhysicsEvent3D> events);
        void DeliverEvents(std::span<const PhysicsEvent2D> events);
        void Update(Timestep timestep);
        // Whether any script had OnLateUpdate, so the transform pass runs again
        bool LateUpdate(Timestep timestep);

        // The entity and its subtree go at DestroyPending, after their scripts' OnDestroy. Physics takes their bodies, colliders and joints out at the next step
        void DestroyEntity(Entity entity);
        // At the end of the frame. An entity destroyed meanwhile is destroyed in the same call
        void DestroyPending();

        [[nodiscard]] Statistics GetStatistics() const;

    private:
        struct State;

        State* m_State = nullptr;
    };
}