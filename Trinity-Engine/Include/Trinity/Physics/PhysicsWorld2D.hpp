#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace Trinity
{
    // The shapes the small body API below makes, for tools and checks. A running scene's 2D bodies take theirs from its colliders
    enum class BodyShape2D : std::uint8_t
    {
        Circle,
        Box
    };

    // A body as PhysicsWorld2D makes it, in the XY plane, in metres, kilograms and seconds, with Y up and angles counter-clockwise in radians. Damping is a fraction of the velocity lost each second
    struct BodyDescription2D
    {
        BodyMotion Motion = BodyMotion::Dynamic;
        BodyShape2D Shape = BodyShape2D::Box;
        // The circle's radius, or the box's half extents
        float Radius = 0.5f;
        glm::vec2 HalfExtents{ 0.5f };
        glm::vec2 Position{ 0.0f };
        float Angle = 0.0f;
        glm::vec2 LinearVelocity{ 0.0f };
        float AngularVelocity = 0.0f;
        // 0 takes the mass from the shape at Box2D's 1 kg per square metre
        float Mass = 0.0f;
        float Friction = 0.6f;
        float Restitution = 0.0f;
        float LinearDamping = 0.0f;
        float AngularDamping = 0.0f;
        float GravityFactor = 1.0f;
        // As the world's CollisionLayers name it, with one past the last taken as the first
        std::uint32_t Layer = 0;
        // Kept with the body for whoever made it, such as an entity's UUID
        std::uint64_t UserData = 0;
    };

    // A body in one 2D world. The default names none
    struct PhysicsBodyID2D
    {
        static constexpr std::uint64_t c_Invalid = 0;

        std::uint64_t Value = c_Invalid;

        [[nodiscard]] constexpr bool IsValid() const { return Value != c_Invalid; }
        constexpr explicit operator bool() const { return IsValid(); }
        [[nodiscard]] constexpr bool operator==(const PhysicsBodyID2D&) const = default;
    };

    struct PhysicsWorldSettings2D
    {
        glm::vec2 Gravity{ 0.0f, -9.81f };
        // The solver's sub-steps in each step, Box2D's own default
        std::uint32_t SubSteps = 4;
        // Which layers' bodies meet, fixed for the world's life
        CollisionLayers Layers;
    };

    // A 2D physics world: Box2D's, with its tasks run on Trinity's job system and its memory under Physics, on the same fixed step as the 3D world. Box2D stays inside the engine, so nothing here names its types. A running scene's 2D physics owns one and steps it at the fixed rate. On the main thread
    class TRINITY_API PhysicsWorld2D
    {
    public:
        explicit PhysicsWorld2D(const PhysicsWorldSettings2D& settings = {});
        ~PhysicsWorld2D();

        PhysicsWorld2D(const PhysicsWorld2D&) = delete;
        PhysicsWorld2D& operator=(const PhysicsWorld2D&) = delete;

        // Added to the world at once, a dynamic body awake
        [[nodiscard]] PhysicsBodyID2D CreateBody(const BodyDescription2D& description);
        void DestroyBody(PhysicsBodyID2D body);

        [[nodiscard]] glm::vec2 GetBodyPosition(PhysicsBodyID2D body) const;
        [[nodiscard]] float GetBodyAngle(PhysicsBodyID2D body) const;
        [[nodiscard]] glm::vec2 GetBodyLinearVelocity(PhysicsBodyID2D body) const;
        [[nodiscard]] bool IsBodyActive(PhysicsBodyID2D body) const;

        // One fixed step of the given length, in the settings' sub-steps
        void Step(Timestep step);

        void SetGravity(const glm::vec2& gravity);
        [[nodiscard]] glm::vec2 GetGravity() const;
        [[nodiscard]] std::uint32_t GetBodyCount() const;
        [[nodiscard]] std::uint32_t GetActiveBodyCount() const;

    private:
        friend class PhysicsScene2D;

        struct State;

        State* m_State = nullptr;
    };
}