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

    // A scene's bodies in a PhysicsWorld3D, made when the scene starts running from its RigidBody and collider components, which are read once then, with the project's collision layers. An entity with a RigidBody is a body, and one with colliders and no RigidBody above it a static body. Colliders on entities below a body, without a RigidBody of their own, join it as one compound shape. A body's trigger colliders become a sensor of their own beside it, which follows it. Dynamic bodies write their poses back to their entities' transforms after each step, local to any parent, and kinematic ones are moved to theirs over the steps of each frame. Both are drawn at their poses between their last two steps. Hull and mesh colliders wait for their shapes to load, and their body is made once all have. Contacts and triggers are gathered on Jolt's workers and handed out on the main thread after each step, in the same order on every run. physics.events logs them to the Console. Queries look at the bodies as the last step left them, on the main thread between steps, as M7's scripts will
    class TRINITY_API PhysicsScene3D
    {
    public:
        explicit PhysicsScene3D(Scene& scene, const CollisionLayers& layers = {});
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
        [[nodiscard]] const CollisionLayers& GetLayers() const;

        // The nearest collider along the ray, within the distance, and every collider it meets, nearest first, each once. The direction need not be of unit length
        [[nodiscard]] std::optional<PhysicsHit3D> Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter = {}) const;
        [[nodiscard]] std::vector<PhysicsHit3D> RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter = {}) const;
        // The shape swept from where it is placed, as for a ray
        [[nodiscard]] std::optional<PhysicsHit3D> ShapeCast(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter = {}) const;
        [[nodiscard]] std::vector<PhysicsHit3D> ShapeCastAll(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter = {}) const;
        // Every collider the shape overlaps where it is placed, each once, in UUID order
        [[nodiscard]] std::vector<PhysicsHit3D> Overlap(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const PhysicsQueryFilter3D& filter = {}) const;

    private:
        struct State;

        State* m_State = nullptr;
    };
}