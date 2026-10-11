#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Core/UUID.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace Trinity
{
    class PhysicsWorld3D;
    class Scene;

    enum class PhysicsEventType : std::uint8_t
    {
        ContactBegin,
        ContactStay,
        ContactEnd,
        TriggerEnter,
        TriggerExit
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(PhysicsEventType type);

    // Between two bodies, named by the entities that own them. A contact's are in UUID order and a trigger's first is the trigger's. A contact's beginning has where the bodies first touched and the normal from the first to the second, in world space, and every other event has zeros
    struct PhysicsEvent3D
    {
        PhysicsEventType Type = PhysicsEventType::ContactBegin;
        UUID First;
        UUID Second;
        glm::vec3 Point{ 0.0f };
        glm::vec3 Normal{ 0.0f };
        // Which of the frame's steps it came from, from 0
        std::uint32_t Step = 0;
    };

    // A scene's bodies in a PhysicsWorld3D, made when the scene starts running from its RigidBody and collider components, which are read once then. An entity with a RigidBody is a body, and one with colliders and no RigidBody above it a static body. Colliders on entities below a body, without a RigidBody of their own, join it as one compound shape. A body's trigger colliders become a sensor of their own beside it, which follows it. Dynamic bodies write their poses back to their entities' transforms after each step, local to any parent, and kinematic ones are moved to theirs over the steps of each frame. Both are drawn at their poses between their last two steps. Hull and mesh colliders wait for their shapes to load, and their body is made once all have. Contacts and triggers are gathered on Jolt's workers and handed out on the main thread after each step, in the same order on every run. physics.events logs them to the Console
    class TRINITY_API PhysicsScene3D
    {
    public:
        explicit PhysicsScene3D(Scene& scene);
        ~PhysicsScene3D();

        PhysicsScene3D(const PhysicsScene3D&) = delete;
        PhysicsScene3D& operator=(const PhysicsScene3D&) = delete;

        // Whether the scene has anything for a world: a RigidBody or a collider
        [[nodiscard]] static bool IsNeeded(Scene& scene);

        // A frame of the given number of steps, each step numbered from 0, then the factor between the last two steps it is drawn at
        void BeginFrame(std::uint32_t steps);
        void Step(Timestep step, std::uint32_t index);
        void EndFrame(float interpolation);

        // The frame's events so far, in the order they happened
        [[nodiscard]] std::span<const PhysicsEvent3D> GetEvents() const;
        [[nodiscard]] PhysicsWorld3D& GetWorld();
        // Bodies made, sensors included, and those still waiting for their shapes
        [[nodiscard]] std::uint32_t GetBodyCount() const;
        [[nodiscard]] std::uint32_t GetWaitingCount() const;

    private:
        struct State;

        State* m_State = nullptr;
    };
}