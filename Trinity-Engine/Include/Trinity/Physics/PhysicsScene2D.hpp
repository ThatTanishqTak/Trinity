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

    // Between two 2D bodies, as a 3D event is: named by the entities that own them, a contact's in UUID order and a trigger's with the trigger's first. A contact's beginning has where the bodies first touched and the normal from the first to the second, in the XY plane, and every other event has zeros
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

    // A scene's 2D bodies in a PhysicsWorld2D, made when the scene starts running from its RigidBody2D and 2D collider components, as the 3D bodies are from theirs, and with the same project layers. An entity with a RigidBody2D is a body, and one with 2D colliders and no RigidBody2D above it a static body. 2D colliders on entities below a body, without a RigidBody2D of their own, are shapes of it, triggers included. A body takes its entity's world X and Y and turn about Z, and dynamic bodies write those back after each step, local to any parent and keeping the entity's depth in Z. Kinematic bodies are moved to their transforms over each frame's steps, and both are drawn between their last two steps. 3D and 2D bodies are kept apart: an entity with both a RigidBody and a RigidBody2D keeps its 3D body, and 2D colliders under a 3D body are left out, each reported. Contacts and triggers come out after each step in the same order on every run. Queries look at the bodies as the last step left them, on the main thread between steps
    class TRINITY_API PhysicsScene2D
    {
    public:
        explicit PhysicsScene2D(Scene& scene, const CollisionLayers& layers = {});
        ~PhysicsScene2D();

        PhysicsScene2D(const PhysicsScene2D&) = delete;
        PhysicsScene2D& operator=(const PhysicsScene2D&) = delete;

        // Whether the scene has anything for a 2D world: a RigidBody2D or a 2D collider
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

        // The nearest collider along the ray, within the distance, and every collider it meets, nearest first, each once. The direction need not be of unit length
        [[nodiscard]] std::optional<PhysicsHit2D> Raycast(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter = {}) const;
        [[nodiscard]] std::vector<PhysicsHit2D> RaycastAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter = {}) const;
        // The shape swept from where it is placed, turned counter-clockwise by the angle in radians, as for a ray
        [[nodiscard]] std::optional<PhysicsHit2D> ShapeCast(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter = {}) const;
        [[nodiscard]] std::vector<PhysicsHit2D> ShapeCastAll(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter = {}) const;
        // Every collider the shape overlaps where it is placed, each once, in UUID order
        [[nodiscard]] std::vector<PhysicsHit2D> Overlap(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const PhysicsQueryFilter2D& filter = {}) const;

    private:
        struct State;

        State* m_State = nullptr;
    };
}