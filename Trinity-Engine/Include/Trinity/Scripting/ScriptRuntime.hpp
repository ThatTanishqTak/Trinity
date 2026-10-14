#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Physics/PhysicsScene2D.hpp"
#include "Trinity/Physics/PhysicsScene3D.hpp"
#include "Trinity/Scripting/Script.hpp"
#include "Trinity/Scripting/ScriptField.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Trinity
{
    class Entity;
    class Scene;

    // Every script instance of a running scene, made from its Script components through each language's backend and called in a fixed order: by when each script was added, which for the scene's own is hierarchy order, and in list order on each entity. A script whose class no loaded code has is kept in its component and does not run, with one warning a class, and one whose call failed, such as a C# script that threw, is called no more while play goes on. SceneRuntime owns one and drives it, and C# reaches the scene of the one made last that still lives. Main thread only
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

        // The runtime made last that still lives, whose scene C# scripts reach, or null
        [[nodiscard]] static ScriptRuntime* GetActive();

        [[nodiscard]] Scene& GetScene();
        [[nodiscard]] const ScriptTime& GetTime() const { return m_Time; }

        // As a frame begins, before CreatePending: its time, and the length of its fixed steps
        void BeginFrame(Timestep timestep, Timestep step);

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

        // The entity and its subtree copied at once, as Scene::DuplicateEntity copies them, with the copy's scripts made at once and given their fields. They have OnCreate before their first call, or at the next CreatePending
        Entity DuplicateEntity(Entity entity);
        // The entity and its subtree go at DestroyPending, after their scripts' OnDestroy. Physics takes their bodies, colliders and joints out at the next step
        void DestroyEntity(Entity entity);

        // The instance of the script at that index of the entity's Script component, when it has one made in that language: a Script for C++ and a GC handle for C#. Null for one whose class is unknown, or that was added to the component without the runtime and waits for CreatePending
        [[nodiscard]] void* GetInstance(Entity entity, std::size_t index, ScriptLanguage language) const;
        // The index of the entity's first script of the class, or -1
        [[nodiscard]] std::int32_t FindScript(Entity entity, ScriptLanguage language, std::string_view className) const;
        // A script of the class last on the entity, which gets a Script component if it has none, with its fields at their defaults. Its instance is made at once, and has OnCreate before its first call, or at the next CreatePending. Its index, or -1, logged, when no loaded code has the class
        std::int32_t AddScript(Entity entity, ScriptLanguage language, std::string_view className);
        // The script at that index goes at DestroyPending, after its OnDestroy, and stays until then. One the runtime has not made yet goes at once. False when there is none
        bool RemoveScript(Entity entity, std::size_t index);

        // At the end of the frame: the scripts removed meanwhile, then the entities destroyed. One removed or destroyed meanwhile goes in the same call
        void DestroyPending();

        [[nodiscard]] Statistics GetStatistics() const;

    private:
        struct State;

        State* m_State = nullptr;
        ScriptTime m_Time;
    };
}