#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace Trinity
{
    // The shapes a body can have until collider components come
    enum class BodyShape3D : std::uint8_t
    {
        Sphere,
        Box
    };

    // A body as PhysicsWorld3D makes it, in metres, kilograms and seconds, with Y up. Damping is a fraction of the velocity lost each second
    struct BodyDescription3D
    {
        BodyMotion Motion = BodyMotion::Dynamic;
        BodyShape3D Shape = BodyShape3D::Box;
        // The sphere's radius, or the box's half extents
        float Radius = 0.5f;
        glm::vec3 HalfExtents{ 0.5f };
        glm::vec3 Position{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 LinearVelocity{ 0.0f };
        glm::vec3 AngularVelocity{ 0.0f };
        // 0 takes the mass from the shape at the density of water, 1000 kg per cubic metre
        float Mass = 0.0f;
        float Friction = 0.2f;
        float Restitution = 0.0f;
        float LinearDamping = 0.0f;
        float AngularDamping = 0.05f;
        float GravityFactor = 1.0f;
        // Kept with the body for whoever made it, such as an entity's UUID
        std::uint64_t UserData = 0;
    };

    // A body in one world. The default names none
    struct PhysicsBodyID
    {
        static constexpr std::uint32_t c_Invalid = 0xFFFFFFFFu;

        std::uint32_t Value = c_Invalid;

        [[nodiscard]] constexpr bool IsValid() const { return Value != c_Invalid; }
        constexpr explicit operator bool() const { return IsValid(); }
        [[nodiscard]] constexpr bool operator==(const PhysicsBodyID&) const = default;
    };

    struct PhysicsWorldSettings3D
    {
        glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
        std::uint32_t MaxBodies = 65536;
        std::uint32_t MaxBodyPairs = 65536;
        std::uint32_t MaxContactConstraints = 10240;
        // Scratch memory a step works in, under Physics, as Jolt's own samples size it
        std::uint32_t StepMemoryBytes = 10u << 20;
    };

    // A 3D physics world: Jolt's physics system, with its body interface and broadphase, stepped on Trinity's job system and allocating under Physics. Jolt stays inside the engine, so nothing here names its types. A running scene has one, made only when it has bodies, and SceneRuntime steps it at the fixed rate. On the main thread
    class TRINITY_API PhysicsWorld3D
    {
    public:
        explicit PhysicsWorld3D(const PhysicsWorldSettings3D& settings = {});
        ~PhysicsWorld3D();

        PhysicsWorld3D(const PhysicsWorld3D&) = delete;
        PhysicsWorld3D& operator=(const PhysicsWorld3D&) = delete;

        // Added to the world at once, a dynamic body awake. Invalid when the world is full
        [[nodiscard]] PhysicsBodyID CreateBody(const BodyDescription3D& description);
        void DestroyBody(PhysicsBodyID body);

        [[nodiscard]] glm::vec3 GetBodyPosition(PhysicsBodyID body) const;
        [[nodiscard]] glm::quat GetBodyRotation(PhysicsBodyID body) const;
        [[nodiscard]] glm::vec3 GetBodyLinearVelocity(PhysicsBodyID body) const;
        [[nodiscard]] bool IsBodyActive(PhysicsBodyID body) const;

        // One fixed step of the given length
        void Step(Timestep step);

        void SetGravity(const glm::vec3& gravity);
        [[nodiscard]] glm::vec3 GetGravity() const;
        [[nodiscard]] std::uint32_t GetBodyCount() const;
        [[nodiscard]] std::uint32_t GetActiveBodyCount() const;

    private:
        struct State;

        State* m_State = nullptr;
    };
}