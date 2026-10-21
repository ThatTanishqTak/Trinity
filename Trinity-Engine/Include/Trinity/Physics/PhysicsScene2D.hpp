#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Trinity
{
    class PhysicsWorld2D;
    class Scene;

    // Between two 2D bodies, as a 3D event is: named by the entities that own them, a contact's in UUID order and a trigger's with the trigger's first. A joint's break names the joint's entity and the entity it was joined to, none for the world. A character's hit names the character and the body it began touching. A contact's beginning has where the bodies first touched and the normal from the first to the second, and a hit where the character touches and the normal of the surface it touches there, out towards the character, in the XY plane, and every other event has zeros
    struct PhysicsEvent2D
    {
        PhysicsEventType Type = PhysicsEventType::ContactBegin;
        UUID First;
        UUID Second;
        glm::vec2 Point{ 0.0f };
        glm::vec2 Normal{ 0.0f };
        // Which of the frame's steps it came from, from 0
        std::uint32_t Step = 0;
    };

    // What a 2D query may hit, as a 3D one: colliders on the layers in the mask, triggers only when it asks for them, and nothing of the entity it passes over
    struct PhysicsQueryFilter2D
    {
        std::uint32_t Layers = c_AllCollisionLayers;
        bool HitTriggers = false;
        UUID Ignore;
    };

    // A 2D collider a query met, as a 3D one: its entity and its body's, and for a cast where it met the collider, the collider's normal there and how far along the cast it was. A cast that starts inside a solid meets it at once, at the start, with the normal against the cast. An overlap has only the entities
    struct PhysicsHit2D
    {
        UUID Entity;
        UUID Body;
        glm::vec2 Point{ 0.0f };
        glm::vec2 Normal{ 0.0f };
        float Distance = 0.0f;
        bool Trigger = false;
    };

    enum class PhysicsQueryShapeType2D : std::uint8_t
    {
        Circle,
        Box,
        Capsule
    };

    // A shape a 2D query casts or overlaps, sized as the 2D colliders are: a circle's radius, a box's half extents, and a capsule's radius and whole height along its Y axis
    struct PhysicsQueryShape2D
    {
        PhysicsQueryShapeType2D Type = PhysicsQueryShapeType2D::Circle;
        float Radius = 0.5f;
        glm::vec2 HalfExtents{ 0.5f };
        float Height = 2.0f;

        [[nodiscard]] static PhysicsQueryShape2D MakeCircle(float radius) { return { PhysicsQueryShapeType2D::Circle, radius, glm::vec2(radius), radius * 2.0f }; }
        [[nodiscard]] static PhysicsQueryShape2D MakeBox(const glm::vec2& halfExtents) { return { PhysicsQueryShapeType2D::Box, 0.0f, halfExtents, 0.0f }; }
        [[nodiscard]] static PhysicsQueryShape2D MakeCapsule(float radius, float height) { return { PhysicsQueryShapeType2D::Capsule, radius, glm::vec2(radius), height }; }
    };

    // What a 2D character stands on as its last step left it, as a 3D one's: whether it is ground it can stand on, and, on any ground, a slope too steep included, the ground's normal, how fast the ground moves at the middle of the character's lower end cap, and the entity whose body it is, none for ground that is no body of the scene's
    struct CharacterGround2D
    {
        bool Grounded = false;
        glm::vec2 Normal{ 0.0f };
        glm::vec2 Velocity{ 0.0f };
        UUID Entity;
    };

    // A scene's 2D bodies in a PhysicsWorld2D, made when the scene starts running from its RigidBody2D, 2D collider and CharacterController2D components, as the 3D bodies are from theirs, and with the same project layers. An entity with a RigidBody2D is a body, and one with 2D colliders and no RigidBody2D above it a static body. An entity with a CharacterController2D is a character, moved in each step before the world steps on Box2D's capsule casts and plane solver, with the stepping up, keeping to the floor, slopes and pushing Jolt gives a 3D one, carried upright by what it stands on as the world steps, its lower end cap keeping its place on it, and met by queries, triggers and bodies through its capsule on a kinematic body, which the colliders following it share, or in Physical mode a dynamic body that cannot turn, holding its capsule and its colliders, given its velocity before the world steps and stepped with the world, finding its ground, climbing and keeping to the floor by its capsule as a 3D physical one does, and riding upright what it stands on. Either way it writes its place back as a dynamic body does, and is drawn between its last two steps. A character hits a body as its capsule begins touching it, once until it moves away from it, which comes out after the step's contacts. A character whose mode changes while the scene runs is made again where it is, keeping its velocity. A one-way collider holds a character only from above. 2D colliders on entities below a body, without a RigidBody2D of their own, are shapes of it, triggers included. A body takes its entity's world X and Y and turn about Z, and dynamic bodies write those back after each step, local to any parent and keeping the entity's depth in Z. Kinematic bodies are moved to their transforms over each frame's steps, and both are drawn between their last two steps. A Joint2D joins the body its entity is part of to the body of the entity it names, or to the world, and one that breaks is let go with a JointBreak event. 3D and 2D bodies are kept apart: an entity with a RigidBody2D and a RigidBody or a CharacterController keeps its 3D body or character, and 2D colliders under a 3D body or character are left out, each reported. A joint whose motor or break limits alone change takes them on in place. Contacts and triggers come out after each step in the same order on every run. The scene's changes are taken in as each frame with steps begins, as each step begins and as a script reaches a body. Queries look at the bodies as the last step left them, on the main thread between steps
    class TRINITY_API PhysicsScene2D
    {
    public:
        explicit PhysicsScene2D(Scene& scene, const CollisionLayers& layers = {});
        ~PhysicsScene2D();

        PhysicsScene2D(const PhysicsScene2D&) = delete;
        PhysicsScene2D& operator=(const PhysicsScene2D&) = delete;

        // Whether the scene has anything for a 2D world: a RigidBody2D, a 2D collider or a CharacterController2D
        [[nodiscard]] static bool IsNeeded(Scene& scene);

        // A frame of the given number of steps, each step numbered from 0, then the factor between the last two steps it is drawn at
        void BeginFrame(std::uint32_t steps);
        void Step(Timestep step, std::uint32_t index);
        void EndFrame(float interpolation);

        // The frame's events so far, in the order they happened
        [[nodiscard]] std::span<const PhysicsEvent2D> GetEvents() const;
        [[nodiscard]] PhysicsWorld2D& GetWorld();
        [[nodiscard]] std::uint32_t GetBodyCount() const;
        [[nodiscard]] const CollisionLayers& GetLayers() const;
        [[nodiscard]] PhysicsBodyCounts GetBodyCounts() const;
        // Whether each step keeps where its bodies touch, at the depth of the body whose contact it is, for drawing. Off until asked for
        void SetGatherContacts(bool gather);
        [[nodiscard]] std::span<const PhysicsContactPoint> GetContactPoints() const;

        // The nearest collider along the ray, within the distance, and every collider it meets, nearest first, each once. The direction need not be of unit length
        [[nodiscard]] std::optional<PhysicsHit2D> Raycast(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter = {}) const;
        [[nodiscard]] std::vector<PhysicsHit2D> RaycastAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter = {}) const;
        // The shape swept from where it is placed, turned counter-clockwise by the angle in radians, as for a ray
        [[nodiscard]] std::optional<PhysicsHit2D> ShapeCast(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter = {}) const;
        [[nodiscard]] std::vector<PhysicsHit2D> ShapeCastAll(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter = {}) const;
        // Every collider the shape overlaps where it is placed, each once, in UUID order
        [[nodiscard]] std::vector<PhysicsHit2D> Overlap(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const PhysicsQueryFilter2D& filter = {}) const;

        // What a script does to a 2D body, as to a 3D one: named by the entity that owns it or one of its colliders' entities, with the scene's changes taken in first, in the XY plane, with turns counter-clockwise in radians. Only a dynamic body takes velocities, forces and impulses, a force or torque lasts for the next step, and an impulse changes the velocity at once. A character is no body for these calls, and only Teleport takes one
        PhysicsBodyResult GetVelocity(UUID entity, glm::vec2& linear, float& angular);
        PhysicsBodyResult SetLinearVelocity(UUID entity, const glm::vec2& velocity);
        PhysicsBodyResult SetAngularVelocity(UUID entity, float velocity);
        PhysicsBodyResult AddForce(UUID entity, const glm::vec2& force);
        PhysicsBodyResult AddForceAtPoint(UUID entity, const glm::vec2& force, const glm::vec2& point);
        PhysicsBodyResult AddTorque(UUID entity, float torque);
        PhysicsBodyResult AddImpulse(UUID entity, const glm::vec2& impulse);
        PhysicsBodyResult AddImpulseAtPoint(UUID entity, const glm::vec2& impulse, const glm::vec2& point);
        PhysicsBodyResult AddAngularImpulse(UUID entity, float impulse);
        // The body or character and its entity there at once, at the entity's depth and turned about Z alone, its velocities kept
        PhysicsBodyResult Teleport(UUID entity, const glm::vec2& position, float angle);
        // Where a kinematic body goes over the next frame's steps, as its entity's transform set from code takes it
        PhysicsBodyResult MoveKinematic(UUID entity, const glm::vec2& position, float angle);
        PhysicsBodyResult SetAwake(UUID entity, bool awake);
        PhysicsBodyResult IsAwake(UUID entity, bool& awake);
        // Whether the entity's 2D joint has broken, or none for an entity with no 2D joint the scene took in
        [[nodiscard]] std::optional<bool> IsJointBroken(UUID entity) const;

        // What a script does to a 2D character, as to a 3D one: named as a body is, which refuses an entity that is part of a body and no character. Its velocity is its own, against what it stands on, in the XY plane, and lasts until changed: gravity adds to it off the ground, standing takes its vertical part, and what it walks into takes the part that goes into it. What it stands on carries it besides. A physical character's velocity also takes what the world and other characters do to its body, so a body that hits it knocks it back, and a kinematic character pushes it, until its velocity is set again
        PhysicsBodyResult SetCharacterVelocity(UUID entity, const glm::vec2& velocity);
        PhysicsBodyResult GetCharacterVelocity(UUID entity, glm::vec2& velocity);
        PhysicsBodyResult GetCharacterGround(UUID entity, CharacterGround2D& ground);
        // Moves the character by the displacement over its next step, besides its velocity, as for a 3D one
        PhysicsBodyResult MoveCharacter(UUID entity, const glm::vec2& displacement);

    private:
        struct State;

        State* m_State = nullptr;
    };
}