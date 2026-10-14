#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Scene/Entity.hpp"

#include <glm/glm.hpp>

namespace Trinity
{
    class ScriptRuntime;

    // What a physics callback is told: the other entity, or none for a joint to the world, and for a contact beginning where it began and its normal, out from this script's entity towards the other. A 2D event's point and normal have 0 in Z
    struct ScriptCollision
    {
        UUID Other;
        glm::vec3 Point{ 0.0f };
        glm::vec3 Normal{ 0.0f };
        bool Is2D = false;
    };

    // The base of a C++ script class, of which the C++ backend makes one instance for each script on an entity once its scene runs. OnCreate runs before any other callback, after the physics bodies exist, and only once the script is enabled. OnFixedUpdate runs each fixed step ahead of physics, the physics callbacks after the step that raised them, OnUpdate once a frame after the steps, and OnLateUpdate after the transform pass. OnDestroy runs when the entity or the scene goes, for a script that had OnCreate. A reload while the scene runs makes the instance again from its fields, and the new one has OnReloaded in place of OnCreate, to rebuild what it keeps outside its fields. Main thread only
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

        // The entity and its subtree go at the end of the frame, after every script has run, so a script may destroy its own entity and finish the call it is in
        void Destroy(Entity entity);

    private:
        friend class NativeScriptBackend;

        Entity m_Entity;
        ScriptRuntime* m_Runtime = nullptr;
        void (*m_Release)(Script* script) = nullptr;
    };
}