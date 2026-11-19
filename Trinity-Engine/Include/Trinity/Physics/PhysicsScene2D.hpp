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

    // As a 3D event. A contact names entities in UUID order, a trigger with the trigger first
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

    // As a 3D query: layers in the mask, triggers only when asked for, nothing of the ignored entity
    struct PhysicsQueryFilter2D
    {
        std::uint32_t Layers = c_AllCollisionLayers;
        bool HitTriggers = false;
        UUID Ignore;
    };

    // As a 3D hit. A cast that starts inside a solid meets it at once, the normal against the cast
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

    // Sized as the 2D colliders are: a capsule's height is whole, along its Y axis
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

    // As a 3D one's. Entity is none for ground that is no body of the scene's
    struct CharacterGround2D
    {
        bool Grounded = false;
        glm::vec2 Normal{ 0.0f };
        glm::vec2 Velocity{ 0.0f };
        UUID Entity;
    };

    // Made when the scene starts running, from its 2D components and the project's layers
    class TRINITY_API PhysicsScene2D
    {
    public:
        explicit PhysicsScene2D(Scene& scene, const CollisionLayers& layers = {});
        ~PhysicsScene2D();

        PhysicsScene2D(const PhysicsScene2D&) = delete;
        PhysicsScene2D& operator=(const PhysicsScene2D&) = delete;

        // Whether the scene has a RigidBody2D, a 2D collider or a CharacterController2D
        [[nodiscard]] static bool IsNeeded(Scene& scene);

        // Steps are numbered from 0. The factor is how far between the last two steps it is drawn
        void BeginFrame(std::uint32_t steps);
        void Step(Timestep step, std::uint32_t index);
        void EndFrame(float interpolation);

        // The frame's events so far, in the order they happened
        [[nodiscard]] std::span<const PhysicsEvent2D> GetEvents() const;
        [[nodiscard]] PhysicsWorld2D& GetWorld();
        [[nodiscard]] std::uint32_t GetBodyCount() const;
        [[nodiscard]] const CollisionLayers& GetLayers() const;
        [[nodiscard]] PhysicsBodyCounts GetBodyCounts() const;
        // By each one's ground as GetCharacterGround reads it
        [[nodiscard]] PhysicsCharacterCounts GetCharacterCounts() const;
        // Keeps where bodies touch each step, for drawing. Off until asked for
        void SetGatherContacts(bool gather);
        [[nodiscard]] std::span<const PhysicsContactPoint> GetContactPoints() const;

        // The nearest first, each once. The direction need not be of unit length
        [[nodiscard]] std::optional<PhysicsHit2D> Raycast(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter = {}) const;
        [[nodiscard]] std::vector<PhysicsHit2D> RaycastAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter = {}) const;
        // The angle is counter-clockwise in radians
        [[nodiscard]] std::optional<PhysicsHit2D> ShapeCast(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter = {}) const;
        [[nodiscard]] std::vector<PhysicsHit2D> ShapeCastAll(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter = {}) const;
        // Each once, in UUID order
        [[nodiscard]] std::vector<PhysicsHit2D> Overlap(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const PhysicsQueryFilter2D& filter = {}) const;

        // As for a 3D body, in the XY plane. Only a dynamic body takes velocities, forces and impulses
        PhysicsBodyResult GetVelocity(UUID entity, glm::vec2& linear, float& angular);
        PhysicsBodyResult SetLinearVelocity(UUID entity, const glm::vec2& velocity);
        PhysicsBodyResult SetAngularVelocity(UUID entity, float velocity);
        PhysicsBodyResult AddForce(UUID entity, const glm::vec2& force);
        PhysicsBodyResult AddForceAtPoint(UUID entity, const glm::vec2& force, const glm::vec2& point);
        PhysicsBodyResult AddTorque(UUID entity, float torque);
        PhysicsBodyResult AddImpulse(UUID entity, const glm::vec2& impulse);
        PhysicsBodyResult AddImpulseAtPoint(UUID entity, const glm::vec2& impulse, const glm::vec2& point);
        PhysicsBodyResult AddAngularImpulse(UUID entity, float impulse);
        // At once, at the entity's depth and turned about Z alone, its velocities kept
        PhysicsBodyResult Teleport(UUID entity, const glm::vec2& position, float angle);
        // Where a kinematic body goes over the next frame's steps
        PhysicsBodyResult MoveKinematic(UUID entity, const glm::vec2& position, float angle);
        PhysicsBodyResult SetAwake(UUID entity, bool awake);
        PhysicsBodyResult IsAwake(UUID entity, bool& awake);
        // None for an entity with no 2D joint the scene took in
        [[nodiscard]] std::optional<bool> IsJointBroken(UUID entity) const;

        // As for a 3D character. Its velocity is its own, against what it stands on, until changed
        PhysicsBodyResult SetCharacterVelocity(UUID entity, const glm::vec2& velocity);
        PhysicsBodyResult GetCharacterVelocity(UUID entity, glm::vec2& velocity);
        PhysicsBodyResult GetCharacterGround(UUID entity, CharacterGround2D& ground);
        // Besides its velocity, as for a 3D one
        PhysicsBodyResult MoveCharacter(UUID entity, const glm::vec2& displacement);

    private:
        struct State;

        State* m_State = nullptr;
    };
}