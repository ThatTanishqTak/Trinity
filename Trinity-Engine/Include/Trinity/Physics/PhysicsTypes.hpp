#pragma once

#include "Trinity/Core/Export.hpp"

#include <glm/glm.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Trinity
{
    // How a body moves: never, only where it is moved to, or under gravity, forces and collisions
    enum class BodyMotion : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic
    };

    // How a character moves: moved through the world by the engine, which nothing that hits it pushes, or as a dynamic body that cannot turn, which what hits it knocks back
    enum class CharacterMode : std::uint8_t
    {
        Kinematic,
        Physical
    };

    // What a 3D or a 2D scene's physics hands out after each step
    enum class PhysicsEventType : std::uint8_t
    {
        ContactBegin,
        ContactStay,
        ContactEnd,
        TriggerEnter,
        TriggerExit,
        JointBreak
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(PhysicsEventType type);

    // What a call a script makes on a body did: done, refused since the entity is part of no body made, such as one still waiting for its shapes, or refused since its body cannot do it, such as a force on a body that is not dynamic
    enum class PhysicsBodyResult : std::uint8_t
    {
        Done,
        NoBody,
        WrongMotion
    };

    // The collision layers a project can have, which colliders name by index
    constexpr std::uint32_t c_MaxCollisionLayers = 16;
    // A mask with every layer in it, bit N for layer N
    constexpr std::uint32_t c_AllCollisionLayers = (1u << c_MaxCollisionLayers) - 1;

    // A project's collision layers: a name for each, and which pairs collide, which by default is every pair. A pair collides both ways or neither. Saved with the project, and taken by a scene as it starts running
    struct CollisionLayers
    {
        // Layer 0 is named Default, and an empty name shows as Layer and its number
        std::array<std::string, c_MaxCollisionLayers> Names{ "Default" };
        // Bit N of a layer's row is set when it collides with layer N
        std::array<std::uint16_t, c_MaxCollisionLayers> Rows = [] { std::array<std::uint16_t, c_MaxCollisionLayers> l_Rows{}; l_Rows.fill(static_cast<std::uint16_t>(c_AllCollisionLayers)); return l_Rows; }();

        [[nodiscard]] bool Collides(std::uint32_t first, std::uint32_t second) const
        {
            return first < c_MaxCollisionLayers && second < c_MaxCollisionLayers && (Rows[first] & (1u << second)) != 0;
        }

        // Both ways, so the matrix stays even
        void SetCollides(std::uint32_t first, std::uint32_t second, bool collides)
        {
            if (first >= c_MaxCollisionLayers || second >= c_MaxCollisionLayers)
            {
                return;
            }

            Rows[first] = static_cast<std::uint16_t>(collides ? Rows[first] | (1u << second) : Rows[first] & ~(1u << second));
            Rows[second] = static_cast<std::uint16_t>(collides ? Rows[second] | (1u << first) : Rows[second] & ~(1u << first));
        }

        // Every layer that collides with any in the mask
        [[nodiscard]] std::uint32_t GetCollidingLayers(std::uint32_t layers) const
        {
            std::uint32_t l_Colliding = 0;
            for (std::uint32_t it_Layers = layers & c_AllCollisionLayers; it_Layers != 0; it_Layers &= it_Layers - 1)
            {
                l_Colliding |= Rows[static_cast<std::size_t>(std::countr_zero(it_Layers))];
            }

            return l_Colliding;
        }

        [[nodiscard]] std::string GetName(std::uint32_t layer) const
        {
            return layer < c_MaxCollisionLayers && !Names[layer].empty() ? Names[layer] : std::format("Layer {}", layer);
        }

        [[nodiscard]] bool operator==(const CollisionLayers&) const = default;
    };

    // A project's physics, which a scene takes as it starts running: each world's gravity in metres per second squared, the fixed steps a second, which the simulation clock keeps between 1 and 1000, and the collision layers both worlds share
    struct PhysicsSettings
    {
        static constexpr std::uint32_t c_DefaultStepRate = 60;

        glm::vec3 Gravity3D{ 0.0f, -9.81f, 0.0f };
        glm::vec2 Gravity2D{ 0.0f, -9.81f };
        std::uint32_t StepRate = c_DefaultStepRate;
        CollisionLayers Layers;

        [[nodiscard]] bool operator==(const PhysicsSettings&) const = default;
    };

    // A world's bodies as it reports them after its last step: all of them, static ones included, and of those that can move, the ones awake and the ones asleep
    struct PhysicsBodyCounts
    {
        std::uint32_t Bodies = 0;
        std::uint32_t Awake = 0;
        std::uint32_t Sleeping = 0;

        [[nodiscard]] bool operator==(const PhysicsBodyCounts&) const = default;
    };

    // Where two bodies touched after the last step, in world space, with the normal from the first body to the second, for drawing
    struct PhysicsContactPoint
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Normal{ 0.0f };
    };
}