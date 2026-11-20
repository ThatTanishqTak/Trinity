#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Physics/PhysicsScene2D.hpp"
#include "Trinity/Physics/PhysicsScene3D.hpp"
#include "Trinity/Scripting/Coroutine.hpp"
#include "Trinity/Scripting/Script.hpp"
#include "Trinity/Scripting/ScriptField.hpp"
#include "Trinity/UI/UIElement.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Trinity
{
    class AudioScene;
    class Entity;
    class Scene;

    // Instances taken from one runtime to go on in another. Those never taken in are freed with it
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

    // Calls scripts in the order they were added. One whose call failed is called no more. Main thread
    class TRINITY_API ScriptRuntime
    {
    public:
        // As Unity's, the most the time scale may be
        static constexpr float c_MaxTimeScale = 100.0f;

        // A call one language makes into the other's script counts to the language that made it
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
            // Timers and coroutines waiting, and handlers of UI events
            std::uint32_t Tasks = 0;
            std::uint32_t UIHandlers = 0;
            LanguageStatistics Cpp;
            LanguageStatistics CSharp;
        };

        // Where an instance's root goes in the world, as Instantiate is given it
        struct InstancePose
        {
            glm::vec3 Position{ 0.0f };
            glm::quat Rotation = glm::identity<glm::quat>();
        };

        // The instances of one prefab made while the runtime ran, and the time each took, in microseconds
        struct InstancingTime
        {
            UUID Prefab;
            std::uint64_t Count = 0;
            double TotalMicroseconds = 0.0;
            double MostMicroseconds = 0.0;
        };

        // The game's UI retains the scene while the runtime lives, so documents its scripts reach stay
        explicit ScriptRuntime(Scene& scene);
        // OnDestroy on every script that had OnCreate, in the order they were made, then UI handlers go
        ~ScriptRuntime();

        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;

        // The runtime made last that still lives, whose scene C# scripts reach, or null
        [[nodiscard]] static ScriptRuntime* GetActive();
        // None for either. A script in the other language is kept and does not run, told of once a runtime
        static void SetLanguage(std::optional<ScriptLanguage> language);
        [[nodiscard]] static std::optional<ScriptLanguage> GetLanguage();

        [[nodiscard]] Scene& GetScene();
        [[nodiscard]] const ScriptTime& GetTime() const { return m_Time; }

        // Before CreatePending: the frame's time as it came and scaled, and the length of its fixed steps
        void BeginFrame(Timestep unscaled, Timestep timestep, Timestep step);
        // SceneRuntime scales the next frame's time by it. False, logged, outside 0 to c_MaxTimeScale
        bool SetTimeScale(float scale);

        // Handed over by SceneRuntime as it makes each. Null for one the scene has none for yet
        void SetPhysics(PhysicsScene3D* physics, PhysicsScene2D* physics2D);
        [[nodiscard]] PhysicsScene3D* GetPhysics() const { return m_Physics; }
        [[nodiscard]] PhysicsScene2D* GetPhysics2D() const { return m_Physics2D; }

        // Handed over by SceneRuntime as it makes the runtime. Null for a runtime made without them
        void SetAudio(AudioScene* audio) { m_Audio = audio; }
        [[nodiscard]] AudioScene* GetAudio() const { return m_Audio; }

        // Called as a frame begins and again after its scripts ran. Returns whether any script was added
        bool CreatePending();
        // After CreatePending and before the fixed steps, so a script made this frame gets its events too
        void RaiseInputEvents();
        // After RaiseInputEvents, in the order they happened. A handler that fails is removed
        void RaiseUIEvents();
        void FixedUpdate(Timestep step);
        // In order, to every script on both entities. A character's hit goes to the character's alone
        void DeliverEvents(std::span<const PhysicsEvent3D> events);
        void DeliverEvents(std::span<const PhysicsEvent2D> events);
        void Update(Timestep timestep);
        // Whether any script had OnLateUpdate, so the transform pass runs again
        bool LateUpdate(Timestep timestep);

        // Before a language's code unloads: fields are written back, instances freed without OnDestroy
        std::uint32_t ReleaseInstances(ScriptLanguage language);
        // After the code loads again. One that had OnCreate gets OnReloaded instead. The number made
        std::uint32_t RecreateInstances(ScriptLanguage language);

        // Interval is in seconds, 0 to run once. The task is freed through its calls. Its ID, never 0
        std::uint64_t StartTask(const void* owner, ScriptTaskKind kind, const ScriptTaskCalls& calls, void* task, const ScriptWait& wait, float interval = 0.0f);
        // False when it was not running. One stopped as it runs is freed once it returns
        bool StopTask(std::uint64_t id);
        // The instance's timers or its coroutines, the number stopped
        std::uint32_t StopTasks(const void* owner, ScriptTaskKind kind);
        [[nodiscard]] bool IsTaskRunning(std::uint64_t id) const;

        // As UIDocument::On adds it. It goes with the instance that added it. Its ID, or 0 if refused
        std::uint64_t AddUIHandler(UUID entity, std::string_view element, UIEventType type, const UIHandlerCalls& calls, void* handler);
        // False when it had gone. One removed as it runs is freed once it returns
        bool RemoveUIHandler(std::uint64_t id);

        // Taken out without OnDestroy, with their tasks and UI handlers. One without instances is left out
        [[nodiscard]] ScriptCarry Extract(std::span<const UUID> entities);
        // Before the first CreatePending, so none is made again. Those whose entity is not here are freed
        void Adopt(ScriptCarry& carry);

        // Copied at once with their scripts, which have OnCreate by the next CreatePending
        Entity DuplicateEntity(Entity entity);
        // As Unity's Instantiate, made at once. Invalid, logged, for a prefab its SceneManager lacks
        Entity InstantiatePrefab(UUID prefab, Entity parent, const std::optional<InstancePose>& pose);
        // Each prefab instanced while the runtime ran, in the order of its first instance
        [[nodiscard]] std::span<const InstancingTime> GetInstancingTimes() const;
        // At DestroyPending, after their scripts' OnDestroy. Physics drops their bodies at the next step
        void DestroyEntity(Entity entity);

        // A Script for C++ and a GC handle for C#. Null for an unknown class, or one not yet made
        [[nodiscard]] void* GetInstance(Entity entity, std::size_t index, ScriptLanguage language) const;
        // Stopped since a call into it failed, until its code reloads. False for one not made yet
        [[nodiscard]] bool IsFaulted(Entity entity, std::size_t index) const;
        // The index of the entity's first script of the class, or -1
        [[nodiscard]] std::int32_t FindScript(Entity entity, ScriptLanguage language, std::string_view className) const;
        // Made at once, with OnCreate by the next CreatePending. Its index, or -1, logged, if unknown
        std::int32_t AddScript(Entity entity, ScriptLanguage language, std::string_view className);
        // At DestroyPending, after its OnDestroy. One not yet made goes at once. False when there is none
        bool RemoveScript(Entity entity, std::size_t index);

        // At the end of the frame: the scripts removed, then the entities destroyed
        void DestroyPending();

        [[nodiscard]] Statistics GetStatistics() const;

    private:
        struct State;

        State* m_State = nullptr;
        ScriptTime m_Time;
        PhysicsScene3D* m_Physics = nullptr;
        PhysicsScene2D* m_Physics2D = nullptr;
        AudioScene* m_Audio = nullptr;
    };
}