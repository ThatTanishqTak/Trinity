#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Trinity
{
    class PhysicsWorld3D;
    class Scene;

    // Between two bodies, named by the entities that own them. A contact's are in UUID order and a trigger's first is the trigger's. A joint's break names the joint's entity and the entity it was joined to, none for the world. A contact's beginning has where the bodies first touched and the normal from the first to the second, in world space, and every other event has zeros
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

    // What a query may hit: colliders on the layers in the mask, triggers only when it asks for them, and nothing of the entity it passes over, such as the one asking, whose body and colliders are all left out
    struct PhysicsQueryFilter3D
    {
        std::uint32_t Layers = c_AllCollisionLayers;
        bool HitTriggers = false;
        UUID Ignore;
    };

    // A collider a query met: its entity and the entity whose body it is part of, which are one for a collider on the body's own entity. A cast's hit has where it met the collider, the collider's normal there, out towards the ray or shape, and how far along the cast it was. A cast that starts inside a solid meets it at once, at the start, with the normal against the cast. An overlap has only the entities
    struct PhysicsHit3D
    {
        UUID Entity;
        UUID Body;
        glm::vec3 Point{ 0.0f };
        glm::vec3 Normal{ 0.0f };
        float Distance = 0.0f;
        bool Trigger = false;
    };

    enum class PhysicsQueryShapeType3D : std::uint8_t
    {
        Sphere,
        Box,
        Capsule
    };

    // A shape a query casts or overlaps, sized as the colliders are: a sphere's radius, a box's half extents, and a capsule's radius and whole height along its Y axis, end caps included
    struct PhysicsQueryShape3D
    {
        PhysicsQueryShapeType3D Type = PhysicsQueryShapeType3D::Sphere;
        float Radius = 0.5f;
        glm::vec3 HalfExtents{ 0.5f };
        float Height = 2.0f;

        [[nodiscard]] static PhysicsQueryShape3D MakeSphere(float radius) { return { PhysicsQueryShapeType3D::Sphere, radius, glm::vec3(radius), radius * 2.0f }; }
        [[nodiscard]] static PhysicsQueryShape3D MakeBox(const glm::vec3& halfExtents) { return { PhysicsQueryShapeType3D::Box, 0.0f, halfExtents, 0.0f }; }
        [[nodiscard]] static PhysicsQueryShape3D MakeCapsule(float radius, float height) { return { PhysicsQueryShapeType3D::Capsule, radius, glm::vec3(radius), height }; }
    };

    // What a character stands on as its last step left it: whether it is ground it can stand on, and, on any ground, a slope too steep included, the ground's normal, how fast the ground moves where the character is, and the entity whose body it is, none for ground that is no body of the scene's
    struct CharacterGround3D
    {
        bool Grounded = false;
        glm::vec3 Normal{ 0.0f };
        glm::vec3 Velocity{ 0.0f };
        UUID Entity;
    };

    // A scene's bodies in a PhysicsWorld3D, made when the scene starts running from its RigidBody, collider and CharacterController components, which are read once then, with the project's collision layers. An entity with a RigidBody is a body, and one with colliders and no RigidBody above it a static body. An entity with a CharacterController is a character, moved by Jolt's CharacterVirtual in each step before the world steps and carried by what it stands on as the world steps, or in Physical mode a dynamic body on Jolt's Character that cannot turn, given its velocity before the world steps and stepped with the world, either way written back to its entity's transform as a dynamic body is, and drawn between its last two steps. A character whose mode changes while the scene runs is made again where it is, keeping its velocity. Colliders on entities below a body, without a RigidBody of their own, join it as one compound shape. A body's trigger colliders become a sensor of their own beside it, which follows it. Dynamic bodies write their poses back to their entities' transforms after each step, local to any parent, and kinematic ones are moved to theirs over the steps of each frame. Both are drawn at their poses between their last two steps. Hull and mesh colliders wait for their shapes to load, and their body is made once all have. Joints are made once both their bodies are, and one past its break force or torque is taken out after the step that broke it, and reported once. A joint whose motor or break limits alone change takes them on in place, and any other change makes it again from where its bodies are. Contacts and triggers are gathered on Jolt's workers and handed out on the main thread after each step, in the same order on every run. physics.events logs them to the Console. The scene's changes are taken in as each frame with steps begins, as each step begins and as a script reaches a body. Queries look at the bodies as the last step left them, on the main thread between steps, as scripts do
    class TRINITY_API PhysicsScene3D
    {
    public:
        explicit PhysicsScene3D(Scene& scene, const CollisionLayers& layers = {});
        ~PhysicsScene3D();

        PhysicsScene3D(const PhysicsScene3D&) = delete;
        PhysicsScene3D& operator=(const PhysicsScene3D&) = delete;

        // Whether the scene has anything for a world: a RigidBody, a collider or a CharacterController
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
        // Joints made and not broken
        [[nodiscard]] std::uint32_t GetJointCount() const;
        [[nodiscard]] const CollisionLayers& GetLayers() const;
        [[nodiscard]] PhysicsBodyCounts GetBodyCounts() const;
        // Whether each step keeps where its bodies touch, triggers left out, for drawing. Off until asked for, since it costs each contact a lock. Jolt reports the contacts of bodies awake, so a sleeping pile shows none
        void SetGatherContacts(bool gather);
        [[nodiscard]] std::span<const PhysicsContactPoint> GetContactPoints() const;

        // The nearest collider along the ray, within the distance, and every collider it meets, nearest first, each once. The direction need not be of unit length
        [[nodiscard]] std::optional<PhysicsHit3D> Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter = {}) const;
        [[nodiscard]] std::vector<PhysicsHit3D> RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter = {}) const;
        // The shape swept from where it is placed, as for a ray
        [[nodiscard]] std::optional<PhysicsHit3D> ShapeCast(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter = {}) const;
        [[nodiscard]] std::vector<PhysicsHit3D> ShapeCastAll(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter = {}) const;
        // Every collider the shape overlaps where it is placed, each once, in UUID order
        [[nodiscard]] std::vector<PhysicsHit3D> Overlap(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const PhysicsQueryFilter3D& filter = {}) const;

        // What a script does to a body, named by the entity that owns it or by one of its colliders' entities, on the main thread between steps, with the scene's changes taken in first, so a body added this frame answers. In world space, in metres, kilograms, seconds and radians. Only a dynamic body takes velocities, forces and impulses, and a static one neither sleeps nor wakes. A force or torque lasts for the next step, so a script adds it in OnFixedUpdate, and an impulse changes the velocity at once. A body pushed is woken. A character takes Teleport and the calls of its own below, and refuses the rest
        PhysicsBodyResult GetVelocity(UUID entity, glm::vec3& linear, glm::vec3& angular);
        PhysicsBodyResult SetLinearVelocity(UUID entity, const glm::vec3& velocity);
        PhysicsBodyResult SetAngularVelocity(UUID entity, const glm::vec3& velocity);
        PhysicsBodyResult AddForce(UUID entity, const glm::vec3& force);
        PhysicsBodyResult AddForceAtPoint(UUID entity, const glm::vec3& force, const glm::vec3& point);
        PhysicsBodyResult AddTorque(UUID entity, const glm::vec3& torque);
        PhysicsBodyResult AddImpulse(UUID entity, const glm::vec3& impulse);
        PhysicsBodyResult AddImpulseAtPoint(UUID entity, const glm::vec3& impulse, const glm::vec3& point);
        PhysicsBodyResult AddAngularImpulse(UUID entity, const glm::vec3& impulse);
        // The body or character and its entity there at once, its velocities kept, drawn there with nothing between
        PhysicsBodyResult Teleport(UUID entity, const glm::vec3& position, const glm::quat& rotation);
        // Where a kinematic body goes over the next frame's steps, pushing what is in its way, as its entity's transform set from code takes it
        PhysicsBodyResult MoveKinematic(UUID entity, const glm::vec3& position, const glm::quat& rotation);
        PhysicsBodyResult SetAwake(UUID entity, bool awake);
        PhysicsBodyResult IsAwake(UUID entity, bool& awake);
        // Whether the entity's joint has broken, or none for an entity with no joint the scene took in
        [[nodiscard]] std::optional<bool> IsJointBroken(UUID entity) const;

        // What a script does to a character, named as a body is, which refuses an entity that is part of a body and no character. Its velocity is its own, against what it stands on, in world space, and lasts until changed: gravity adds to it off the ground, standing takes its vertical part, and what it walks into takes the part that goes into it. What it stands on carries it besides. A physical character's velocity also takes what the world and other characters do to its body, so a body that hits it knocks it back, and a kinematic character pushes it, until its velocity is set again
        PhysicsBodyResult SetCharacterVelocity(UUID entity, const glm::vec3& velocity);
        PhysicsBodyResult GetCharacterVelocity(UUID entity, glm::vec3& velocity);
        PhysicsBodyResult GetCharacterGround(UUID entity, CharacterGround3D& ground);

    private:
        struct State;

        State* m_State = nullptr;
    };
}