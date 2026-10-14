#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scripting/ScriptField.hpp"

#include <glm/glm.hpp>

#include <concepts>
#include <cstdint>
#include <string_view>

namespace Trinity
{
    class Scene;
    class ScriptRuntime;

    // What a physics callback is told: the other entity, or none for a joint to the world, and for a contact beginning where it began and its normal, out from this script's entity towards the other. A 2D event's point and normal have 0 in Z
    struct ScriptCollision
    {
        UUID Other;
        glm::vec3 Point{ 0.0f };
        glm::vec3 Normal{ 0.0f };
        bool Is2D = false;
    };

    // A running scene's clock as its scripts see it: this frame's time, which OnUpdate is given, the length of a fixed step, which OnFixedUpdate is given, the scene's time since it started running, and the frames and fixed steps it has run, each counted as it begins. Step in play mode is one frame of one step's time, with one fixed step
    struct ScriptTime
    {
        float DeltaTime = 0.0f;
        float FixedDeltaTime = 0.0f;
        double Elapsed = 0.0;
        std::uint64_t FrameCount = 0;
        std::uint64_t StepCount = 0;
    };

    namespace Detail
    {
        // An address of its own for each C++ script class, which the code that registers the class and asks for it share. Writable, so a linker that folds identical constants leaves each its own
        template<typename T>
        inline char s_ScriptType = 0;
    }

    // The base of a C++ script class, of which the C++ backend makes one instance for each script on an entity once its scene runs. OnCreate runs before any other callback, after the physics bodies exist, and only once the script is enabled. OnFixedUpdate runs each fixed step ahead of physics, the physics callbacks after the step that raised them, OnUpdate once a frame after the steps, and OnLateUpdate after the transform pass. OnDestroy runs when the entity or the scene goes, for a script that had OnCreate. A reload while the scene runs makes the instance again from its fields, and the new one has OnReloaded in place of OnCreate, to rebuild what it keeps outside its fields
    //
    // What a script reaches beyond its entity's components, which it changes through Entity: the running scene's entities, made, copied and destroyed through the runtime so their scripts run, the scene's clock, and the other scripts on an entity. Input and DebugDraw are the engine's own. Main thread only
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

        [[nodiscard]] Entity GetEntity() const { return m_Entity; }
        [[nodiscard]] Scene& GetScene() const;
        [[nodiscard]] const ScriptTime& GetTime() const;

        // Invalid when the scene has no such entity
        [[nodiscard]] Entity FindEntity(UUID id) const;
        // The first in hierarchy order with that tag
        [[nodiscard]] Entity FindEntityByName(std::string_view name) const;
        // At once, last under the parent, or last of the roots without one
        Entity CreateEntity(std::string_view name = "Entity", Entity parent = {});
        // The entity and its subtree copied at once, placed after it, with entity fields inside the copy pointing at the copy's own entities. The copy's scripts are made at once with their fields, and have OnCreate before their first call
        Entity Duplicate(Entity entity);
        // The entity and its subtree go at the end of the frame, after every script has run, so a script may destroy its own entity and finish the call it is in
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

        // Whether the entity has a script of the class, in either language, whether or not its code is loaded
        [[nodiscard]] bool HasScript(Entity entity, ScriptLanguage language, std::string_view className) const;

        // A script of the class last on the entity, with its fields at their defaults, made at once and given OnCreate before its first call. Null, logged, when the class is not registered
        template<std::derived_from<Script> T>
        T* AddScript(Entity entity)
        {
            return static_cast<T*>(AddNativeScript(entity, &Detail::s_ScriptType<T>));
        }

        // False, logged, when no loaded code has the class
        bool AddScript(Entity entity, ScriptLanguage language, std::string_view className);

        // The first script of the class on the entity goes at the end of the frame, after its OnDestroy. False when the entity has none
        template<std::derived_from<Script> T>
        bool RemoveScript(Entity entity)
        {
            return RemoveNativeScript(entity, &Detail::s_ScriptType<T>);
        }

        bool RemoveScript(Entity entity, ScriptLanguage language, std::string_view className);

    private:
        friend class NativeScriptBackend;

        [[nodiscard]] Script* FindNativeScript(Entity entity, const void* type) const;
        Script* AddNativeScript(Entity entity, const void* type);
        bool RemoveNativeScript(Entity entity, const void* type);

        Entity m_Entity;
        ScriptRuntime* m_Runtime = nullptr;
        void (*m_Release)(Script* script) = nullptr;
        // Detail::s_ScriptType of the class it was made as
        const void* m_Type = nullptr;
    };
}