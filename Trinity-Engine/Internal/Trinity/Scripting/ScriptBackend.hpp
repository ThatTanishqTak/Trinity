#pragma once

#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scripting/Script.hpp"
#include "Trinity/Scripting/ScriptClassRegistry.hpp"

namespace Trinity
{
    class ScriptRuntime;

    // One language's side of scripting: it makes and frees instances of the classes its code registered and calls their lifecycle. An instance is opaque to the runtime, a pointer for C++ and a GC handle for C#. The runtime calls only the callbacks the class has, and only on the main thread
    class ScriptBackend
    {
    public:
        virtual ~ScriptBackend() = default;

        [[nodiscard]] virtual ScriptLanguage GetLanguage() const = 0;

        // An instance of the class for the script on the entity, or null, logged, when none can be made
        [[nodiscard]] virtual void* CreateInstance(const ScriptClassDescription& description, Entity entity, ScriptRuntime& runtime) = 0;
        virtual void DestroyInstance(void* instance) = 0;

        virtual void OnCreate(void* instance) = 0;
        virtual void OnFixedUpdate(void* instance, Timestep step) = 0;
        virtual void OnUpdate(void* instance, Timestep timestep) = 0;
        virtual void OnLateUpdate(void* instance, Timestep timestep) = 0;
        virtual void OnDestroy(void* instance) = 0;
        virtual void OnPhysicsEvent(void* instance, PhysicsEventType type, const ScriptCollision& collision) = 0;
    };

    // The backend of each language the engine has, made after the script class registry and destroyed before it. C# joins in M7 step 7
    namespace ScriptBackends
    {
        void Initialize();
        void Shutdown();

        // Null for a language with no backend
        [[nodiscard]] ScriptBackend* Find(ScriptLanguage language);
    }
}