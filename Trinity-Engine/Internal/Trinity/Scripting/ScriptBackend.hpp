#pragma once

#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scripting/Script.hpp"
#include "Trinity/Scripting/ScriptClassRegistry.hpp"

namespace Trinity
{
    class Scene;
    class ScriptRuntime;
    struct ScriptEntry;

    // An instance is a pointer for C++ and a GC handle for C#. Called only on the main thread
    class ScriptBackend
    {
    public:
        virtual ~ScriptBackend() = default;

        [[nodiscard]] virtual ScriptLanguage GetLanguage() const = 0;

        // An instance of the class for the script on the entity, or null, logged, when none can be made
        [[nodiscard]] virtual void* CreateInstance(const ScriptClassDescription& description, Entity entity, ScriptRuntime& runtime) = 0;
        virtual void DestroyInstance(void* instance) = 0;
        // An instance that moved into another runtime with its entity, under the same UUID
        virtual void Rebind(void* instance, Entity entity, ScriptRuntime& runtime) = 0;
        // This and each callback below are false when the script failed, which the backend has logged
        [[nodiscard]] virtual bool ApplyFields(void* instance, const ScriptClassDescription& description, const ScriptEntry& entry, Scene& scene) = 0;
        // Each field as the instance has it now, into the entry, before a reload frees the instance
        virtual void ReadFields(const void* instance, const ScriptClassDescription& description, ScriptEntry& entry) = 0;

        [[nodiscard]] virtual bool OnCreate(void* instance) = 0;
        [[nodiscard]] virtual bool OnFixedUpdate(void* instance, Timestep step) = 0;
        [[nodiscard]] virtual bool OnUpdate(void* instance, Timestep timestep) = 0;
        [[nodiscard]] virtual bool OnLateUpdate(void* instance, Timestep timestep) = 0;
        [[nodiscard]] virtual bool OnDestroy(void* instance) = 0;
        [[nodiscard]] virtual bool OnReloaded(void* instance) = 0;
        [[nodiscard]] virtual bool OnPhysicsEvent(void* instance, PhysicsEventType type, const ScriptCollision& collision) = 0;

        // Once a frame. Nothing for a language whose scripts read their actions as they run
        virtual void RaiseInputEvents() {}
    };

    // The C# backend, made and destroyed with the others, with classes while a game assembly is loaded
    namespace ManagedScriptBackend
    {
        void Initialize();
        void Shutdown();

        [[nodiscard]] ScriptBackend* Get();
    }

    // One for each language, made after the script class registry and destroyed before it
    namespace ScriptBackends
    {
        void Initialize();
        void Shutdown();

        // Null for a language with no backend
        [[nodiscard]] ScriptBackend* Find(ScriptLanguage language);
    }
}