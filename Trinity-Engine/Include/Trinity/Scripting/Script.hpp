#pragma once

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Prefab.hpp"
#include "Trinity/Scripting/Coroutine.hpp"
#include "Trinity/Scripting/ScriptField.hpp"
#include "Trinity/UI/UIElement.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Trinity
{
    class AudioMixer;
    class AudioScene;
    class PhysicsScene2D;
    class PhysicsScene3D;
    class Scene;
    class ScriptRuntime;

    // A contact's normal points from this entity to the other, a character hit's towards the character
    struct ScriptCollision
    {
        UUID Other;
        glm::vec3 Point{ 0.0f };
        glm::vec3 Normal{ 0.0f };
        bool Is2D = false;
    };

    // Frame and scene time are scaled by the time scale, as Unity's. The unscaled ones are as they came
    struct ScriptTime
    {
        float DeltaTime = 0.0f;
        float FixedDeltaTime = 0.0f;
        double Elapsed = 0.0;
        std::uint64_t FrameCount = 0;
        std::uint64_t StepCount = 0;
        float TimeScale = 1.0f;
        float UnscaledDeltaTime = 0.0f;
        double UnscaledElapsed = 0.0;
    };

    namespace Detail
    {
        // An address of its own for each class. Writable, so a linker folding constants keeps them apart
        template<typename T>
        inline char s_ScriptType = 0;
    }

    class TRINITY_API Script
    {
    public:
        Script() = default;
        virtual ~Script() = default;

        Script(const Script&) = delete;
        Script& operator=(const Script&) = delete;

        virtual void OnCreate() {}
        virtual void OnFixedUpdate([[maybe_unused]] Timestep step) {}
        virtual void OnUpdate([[maybe_unused]] Timestep timestep) {}
        virtual void OnLateUpdate([[maybe_unused]] Timestep timestep) {}
        virtual void OnDestroy() {}
        virtual void OnReloaded() {}

        virtual void OnContactBegin([[maybe_unused]] const ScriptCollision& collision) {}
        virtual void OnContactStay([[maybe_unused]] const ScriptCollision& collision) {}
        virtual void OnContactEnd([[maybe_unused]] const ScriptCollision& collision) {}
        virtual void OnTriggerEnter([[maybe_unused]] const ScriptCollision& collision) {}
        virtual void OnTriggerExit([[maybe_unused]] const ScriptCollision& collision) {}
        virtual void OnJointBreak([[maybe_unused]] const ScriptCollision& collision) {}
        // The entity's character began touching another body in the step. Once, until it moves away
        virtual void OnCharacterHit([[maybe_unused]] const ScriptCollision& hit) {}

        [[nodiscard]] Entity GetEntity() const { return m_Entity; }
        [[nodiscard]] Scene& GetScene() const;
        [[nodiscard]] const ScriptTime& GetTime() const;
        // As Unity's Time.timeScale, from the next frame on. False, logged, outside 0 to c_MaxTimeScale
        bool SetTimeScale(float scale);
        // The running scene's physics. Null while the scene has no body of that kind
        [[nodiscard]] PhysicsScene3D* GetPhysics() const;
        [[nodiscard]] PhysicsScene2D* GetPhysics2D() const;
        // The running scene's sounds. Null while no scene's sounds run with the script
        [[nodiscard]] AudioScene* GetAudio() const;
        // The project's mixer. Null while the scene's sounds play on no engine
        [[nodiscard]] AudioMixer* GetAudioMixer() const;

        // Loaded at once if no view shows the scene yet, so OnCreate finds its elements
        [[nodiscard]] UIDocument GetUIDocument(Entity entity) const;
        [[nodiscard]] UIDocument GetUIDocument() const { return GetUIDocument(m_Entity); }

        // Invalid when the scene has no such entity
        [[nodiscard]] Entity FindEntity(UUID id) const;
        // The first in hierarchy order with that tag
        [[nodiscard]] Entity FindEntityByName(std::string_view name) const;
        // At once, last under the parent, or last of the roots without one
        Entity CreateEntity(std::string_view name = "Entity", Entity parent = {});
        // Copied at once, placed after it. The copy's scripts have OnCreate by the end of the frame
        Entity Duplicate(Entity entity);
        // As Unity's Instantiate, made at once. Invalid, logged, for a prefab the running scene lacks
        Entity Instantiate(const AssetRef<PrefabAsset>& prefab, Entity parent = {});
        Entity Instantiate(const AssetRef<PrefabAsset>& prefab, const glm::vec3& position, const glm::quat& rotation, Entity parent = {});
        // At the end of the frame, after every script has run, so a script may destroy its own entity
        void Destroy(Entity entity);

        // The first C++ script of exactly that class on the entity, or null
        template<std::derived_from<Script> T>
        [[nodiscard]] T* GetScript(Entity entity) const
        {
            return static_cast<T*>(FindNativeScript(entity, &Detail::s_ScriptType<T>));
        }

        template<std::derived_from<Script> T>
        [[nodiscard]] T* GetScript() const
        {
            return GetScript<T>(m_Entity);
        }

        // Whether the entity has a script of the class, whether or not its code is loaded
        [[nodiscard]] bool HasScript(Entity entity, ScriptLanguage language, std::string_view className) const;

        // Made at once, with OnCreate by the end of the frame. Null, logged, for an unregistered class
        template<std::derived_from<Script> T>
        T* AddScript(Entity entity)
        {
            return static_cast<T*>(AddNativeScript(entity, &Detail::s_ScriptType<T>));
        }

        // False, logged, when no loaded code has the class
        bool AddScript(Entity entity, ScriptLanguage language, std::string_view className);

        // The first of the class goes at the end of the frame, after OnDestroy. False if there is none
        template<std::derived_from<Script> T>
        bool RemoveScript(Entity entity)
        {
            return RemoveNativeScript(entity, &Detail::s_ScriptType<T>);
        }

        bool RemoveScript(Entity entity, ScriptLanguage language, std::string_view className);

        // At the end of the frame once it has loaded, as SceneManager does. False, logged, if unknown
        bool LoadScene(UUID scene);
        bool LoadScene(std::string_view path);

        // In seconds of the scene's fixed steps. An interval must be above 0. Empty, logged, with no scene
        template<typename F>
            requires std::is_invocable_v<std::decay_t<F>&>
        TimerHandle Invoke(F&& callback, float delay)
        {
            using Callback = Detail::TimerCallback<std::decay_t<F>>;

            return StartTimer(Callback::c_Calls, Memory::New<Callback>(MemoryTag::Scripting, std::forward<F>(callback)), delay, 0.0f, false);
        }

        template<typename F>
            requires std::is_invocable_v<std::decay_t<F>&>
        TimerHandle InvokeRepeating(F&& callback, float delay, float interval)
        {
            using Callback = Detail::TimerCallback<std::decay_t<F>>;

            return StartTimer(Callback::c_Calls, Memory::New<Callback>(MemoryTag::Scripting, std::forward<F>(callback)), delay, interval, true);
        }

        // False when it had already run for the last time or been cancelled
        bool CancelInvoke(TimerHandle timer);
        void CancelInvokes();
        [[nodiscard]] bool IsPending(TimerHandle timer) const;

        // Runs at once until it first waits. Empty if it finished without waiting, or no scene runs it
        CoroutineHandle StartCoroutine(Coroutine coroutine);
        // False when it had already finished or been stopped. One that stops itself goes once it next waits
        bool StopCoroutine(CoroutineHandle coroutine);
        void StopAllCoroutines();
        [[nodiscard]] bool IsRunning(CoroutineHandle coroutine) const;

    private:
        friend class NativeScriptBackend;

        [[nodiscard]] Script* FindNativeScript(Entity entity, const void* type) const;
        Script* AddNativeScript(Entity entity, const void* type);
        bool RemoveNativeScript(Entity entity, const void* type);
        TimerHandle StartTimer(const ScriptTaskCalls& calls, void* callback, float delay, float interval, bool repeating);

        Entity m_Entity;
        ScriptRuntime* m_Runtime = nullptr;
        void (*m_Release)(Script* script) = nullptr;
        // Detail::s_ScriptType of the class it was made as
        const void* m_Type = nullptr;
    };
}