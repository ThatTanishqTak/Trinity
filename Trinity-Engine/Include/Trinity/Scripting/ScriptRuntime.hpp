#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Physics/PhysicsScene2D.hpp"
#include "Trinity/Physics/PhysicsScene3D.hpp"
#include "Trinity/Scripting/Coroutine.hpp"
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

    // Script instances taken out of one runtime with their entities, with their waiting timers and coroutines, to go on in another, as SceneManager carries the entities kept across scenes. Those never taken in are freed with it, without OnDestroy
    class TRINITY_API ScriptCarry
    {
    public:
        ScriptCarry() = default;
        ~ScriptCarry();

        ScriptCarry(ScriptCarry&& other) noexcept;
        ScriptCarry& operator=(ScriptCarry&& other) noexcept;
        ScriptCarry(const ScriptCarry&) = delete;
        ScriptCarry& operator=(const ScriptCarry&) = delete;

        [[nodiscard]] std::size_t GetInstanceCount() const;
        [[nodiscard]] std::size_t GetTaskCount() const;

    private:
        friend class ScriptRuntime;

        struct Data;

        void Free();

        Data* m_Data = nullptr;
    };

    // Every script instance of a running scene, made from its Script components through each language's backend and called in a fixed order: by when each script was added, which for the scene's own is hierarchy order, and in list order on each entity. A script whose class no loaded code has is kept in its component and does not run, with one warning a class, and one whose call failed, such as a C# script that threw, is called no more while play goes on. SceneRuntime owns one and drives it, and C# reaches the scene of the one made last that still lives. Main thread only
    class TRINITY_API ScriptRuntime
    {
    public:
        // One language's share: its instances, its timers and coroutines waiting, and the time the last frame spent in its scripts, from the time each call into it began to the next call into the other language or the end of the runtime's work for that part of the frame. A call one language makes into the other's script counts to the language that made it
        struct LanguageStatistics
        {
            std::uint32_t Instances = 0;
            std::uint32_t Timers = 0;
            std::uint32_t Coroutines = 0;
            float Milliseconds = 0.0f;
        };

        struct Statistics
        {
            std::uint32_t Instances = 0;
            // Scripts that do not run, since no loaded code has their class
            std::uint32_t Unknown = 0;
            // Of the instances, those stopped since a call into them failed, such as a C# script that threw
            std::uint32_t Faulted = 0;
            // Timers and coroutines waiting
            std::uint32_t Tasks = 0;
            LanguageStatistics Cpp;
            LanguageStatistics CSharp;
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

        // The scene's physics worlds, which SceneRuntime hands over as it makes each, and null for one the scene has none for yet
        void SetPhysics(PhysicsScene3D* physics, PhysicsScene2D* physics2D);
        [[nodiscard]] PhysicsScene3D* GetPhysics() const { return m_Physics; }
        [[nodiscard]] PhysicsScene2D* GetPhysics2D() const { return m_Physics2D; }

        // Makes the instances of every Script component added since the last call, the scene's own on the first, gives each the values of its fields, and calls OnCreate on each enabled one. A script that a call makes is made in the same call
        void CreatePending();
        void FixedUpdate(Timestep step);
        // A step's events, in the order they happened, to every script on both entities
        void DeliverEvents(std::span<const PhysicsEvent3D> events);
        void DeliverEvents(std::span<const PhysicsEvent2D> events);
        void Update(Timestep timestep);
        // Whether any script had OnLateUpdate, so the transform pass runs again
        bool LateUpdate(Timestep timestep);

        // Before one language's code unloads for a reload: each of its instances writes its fields back into its script, so what it changed while running is kept, and is freed without OnDestroy. Their waiting timers and coroutines are cancelled, with one line saying how many. The scripts wait without instances, as scripts of an unknown class do. The number freed
        std::uint32_t ReleaseInstances(ScriptLanguage language);
        // Once the language's code is loaded again: each of its scripts without an instance whose class is loaded now gets one, with its fields. One that had OnCreate gets OnReloaded instead, and one that never ran has OnCreate before its first call. The number made
        std::uint32_t RecreateInstances(ScriptLanguage language);

        // A timer or a coroutine of the instance, a C++ Script or a C# GC handle, waiting for its first wait, and for a timer that repeats every interval after it, in seconds, 0 for one that runs once. A timer's wait is in seconds. Due timers run in each fixed step after the scripts' OnFixedUpdate, as do coroutines waiting for it, and every other coroutine after the scripts' OnUpdate, each in the order they were started. A script's timers and coroutines go when it does, and when a reload frees its instance, and stop once it has failed. The task is freed through its calls once it finishes or is stopped. Its ID, never 0
        std::uint64_t StartTask(const void* owner, ScriptTaskKind kind, const ScriptTaskCalls& calls, void* task, const ScriptWait& wait, float interval = 0.0f);
        // False when it was not running, having finished or been stopped. One stopped as it runs is freed once it returns
        bool StopTask(std::uint64_t id);
        // The instance's timers or its coroutines, the number stopped
        std::uint32_t StopTasks(const void* owner, ScriptTaskKind kind);
        [[nodiscard]] bool IsTaskRunning(std::uint64_t id) const;

        // The instances of the entities' scripts taken out, without OnDestroy, to go on in another runtime, with their timers and coroutines. An entity whose scripts have no instances yet is left out
        [[nodiscard]] ScriptCarry Extract(std::span<const UUID> entities);
        // Takes carried instances in, each entity now in this runtime's scene under the same UUID, before the first CreatePending, so none is made again or has OnCreate. A C++ script's GetEntity is its entity here, and an Entity it kept elsewhere is of the scene it left. Their timers and coroutines go on with what was left of their waits, on this runtime's clock. Those whose entity is not here are freed with the carry
        void Adopt(ScriptCarry& carry);

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

        // At the end of the frame: the scripts removed meanwhile, then the entities destroyed. One removed or destroyed meanwhile goes in the same call. The frame's script time is then what GetStatistics gives until the next frame ends
        void DestroyPending();

        [[nodiscard]] Statistics GetStatistics() const;

    private:
        struct State;

        State* m_State = nullptr;
        ScriptTime m_Time;
        PhysicsScene3D* m_Physics = nullptr;
        PhysicsScene2D* m_Physics2D = nullptr;
    };
}