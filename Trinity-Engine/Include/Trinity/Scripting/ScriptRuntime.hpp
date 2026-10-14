#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Physics/PhysicsScene2D.hpp"
#include "Trinity/Physics/PhysicsScene3D.hpp"
#include "Trinity/Scripting/ScriptField.hpp"

#include <cstdint>
#include <span>

namespace Trinity
{
    class Entity;
    class Scene;

    // Every script instance of a running scene, made from its Script components through each language's backend and called in a fixed order: by when each script was added, which for the scene's own is hierarchy order, and in list order on each entity. A script whose class no loaded code has is kept in its component and does not run, with one warning a class, and one whose call failed, such as a C# script that threw, is called no more while play goes on. SceneRuntime owns one and drives it. Main thread only
    class TRINITY_API ScriptRuntime
    {
    public:
        struct Statistics
        {
            std::uint32_t Instances = 0;
            // Scripts that do not run, since no loaded code has their class
            std::uint32_t Unknown = 0;
            // Of the instances, those stopped since a call into them failed, such as a C# script that threw
            std::uint32_t Faulted = 0;
        };

        explicit ScriptRuntime(Scene& scene);
        // OnDestroy on every script that had OnCreate, in the order they were made
        ~ScriptRuntime();

        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;

        // Makes the instances of every Script component added since the last call, the scene's own on the first, gives each the values of its fields, and calls OnCreate on each enabled one. A script that a call makes is made in the same call
        void CreatePending();
        void FixedUpdate(Timestep step);
        // A step's events, in the order they happened, to every script on both entities
        void DeliverEvents(std::span<const PhysicsEvent3D> events);
        void DeliverEvents(std::span<const PhysicsEvent2D> events);
        void Update(Timestep timestep);
        // Whether any script had OnLateUpdate, so the transform pass runs again
        bool LateUpdate(Timestep timestep);

        // Before one language's code unloads for a reload: each of its instances writes its fields back into its script, so what it changed while running is kept, and is freed without OnDestroy. The scripts wait without instances, as scripts of an unknown class do. The number freed
        std::uint32_t ReleaseInstances(ScriptLanguage language);
        // Once the language's code is loaded again: each of its scripts without an instance whose class is loaded now gets one, with its fields. One that had OnCreate gets OnReloaded instead, and one that never ran has OnCreate before its first call. The number made
        std::uint32_t RecreateInstances(ScriptLanguage language);

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