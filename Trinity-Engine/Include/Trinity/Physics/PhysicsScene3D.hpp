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

    // Named by the entities that own the bodies: a contact's in UUID order, a trigger's first
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

    // Layers in the mask, triggers only when asked for, and nothing of the ignored entity's body
    struct PhysicsQueryFilter3D
    {
        std::uint32_t Layers = c_AllCollisionLayers;
        bool HitTriggers = false;
        UUID Ignore;
    };

    // A cast that starts inside a solid meets it at once, with the normal against the cast
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

    // Sized as the colliders are: a capsule's height is whole, along its Y axis
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

    // As its last step left it. Entity is none for ground that is no body of the scene's
    struct CharacterGround3D
    {
        bool Grounded = false;
        glm::vec3 Normal{ 0.0f };
        glm::vec3 Velocity{ 0.0f };
        UUID Entity;
    };

    // Made when the scene starts running, from its components and the project's collision layers
    class TRINITY_API PhysicsScene3D
    {
    public:
        explicit PhysicsScene3D(Scene& scene, const CollisionLayers& layers = {});
        ~PhysicsScene3D();

        PhysicsScene3D(const PhysicsScene3D&) = delete;
        PhysicsScene3D& operator=(const PhysicsScene3D&) = delete;

        // Whether the scene has anything for a world: a RigidBody, a collider or a CharacterController
        [[nodiscard]] static bool IsNeeded(Scene& scene);

        // Steps are numbered from 0. The factor is how far between the last two steps it is drawn
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
        // By each one's ground as GetCharacterGround reads it
        [[nodiscard]] PhysicsCharacterCounts GetCharacterCounts() const;
        // Off until asked for, as it costs each contact a lock. Jolt reports only awake bodies' contacts
        void SetGatherContacts(bool gather);
        [[nodiscard]] std::span<const PhysicsContactPoint> GetContactPoints() const;

        // The nearest first, each once. The direction need not be of unit length
        [[nodiscard]] std::optional<PhysicsHit3D> Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter = {}) const;
        [[nodiscard]] std::vector<PhysicsHit3D> RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter = {}) const;
        // The shape swept from where it is placed, as for a ray
        [[nodiscard]] std::optional<PhysicsHit3D> ShapeCast(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter = {}) const;
        [[nodiscard]] std::vector<PhysicsHit3D> ShapeCastAll(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter = {}) const;
        // Each once, in UUID order
        [[nodiscard]] std::vector<PhysicsHit3D> Overlap(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const PhysicsQueryFilter3D& filter = {}) const;

        // Main thread, between steps. World space, in metres, kilograms, seconds and radians
        PhysicsBodyResult GetVelocity(UUID entity, glm::vec3& linear, glm::vec3& angular);
        PhysicsBodyResult SetLinearVelocity(UUID entity, const glm::vec3& velocity);
        PhysicsBodyResult SetAngularVelocity(UUID entity, const glm::vec3& velocity);
        PhysicsBodyResult AddForce(UUID entity, const glm::vec3& force);
        PhysicsBodyResult AddForceAtPoint(UUID entity, const glm::vec3& force, const glm::vec3& point);
        PhysicsBodyResult AddTorque(UUID entity, const glm::vec3& torque);
        PhysicsBodyResult AddImpulse(UUID entity, const glm::vec3& impulse);
        PhysicsBodyResult AddImpulseAtPoint(UUID entity, const glm::vec3& impulse, const glm::vec3& point);
        PhysicsBodyResult AddAngularImpulse(UUID entity, const glm::vec3& impulse);
        // At once, its velocities kept, drawn there with nothing between
        PhysicsBodyResult Teleport(UUID entity, const glm::vec3& position, const glm::quat& rotation);
        // Where a kinematic body goes over the next frame's steps, pushing what is in its way
        PhysicsBodyResult MoveKinematic(UUID entity, const glm::vec3& position, const glm::quat& rotation);
        PhysicsBodyResult SetAwake(UUID entity, bool awake);
        PhysicsBodyResult IsAwake(UUID entity, bool& awake);
        // None for an entity with no joint the scene took in
        [[nodiscard]] std::optional<bool> IsJointBroken(UUID entity) const;

        // Its velocity is its own, against what it stands on, in world space, and lasts until changed
        PhysicsBodyResult SetCharacterVelocity(UUID entity, const glm::vec3& velocity);
        PhysicsBodyResult GetCharacterVelocity(UUID entity, glm::vec3& velocity);
        PhysicsBodyResult GetCharacterGround(UUID entity, CharacterGround3D& ground);
        // Besides its velocity. Moves asked for before the step add up. Teleport is at once
        PhysicsBodyResult MoveCharacter(UUID entity, const glm::vec3& displacement);

    private:
        struct State;

        State* m_State = nullptr;
    };
}