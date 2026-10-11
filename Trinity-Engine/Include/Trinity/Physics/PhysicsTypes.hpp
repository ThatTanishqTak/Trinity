#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Trinity
{
    // How a body moves: never, only where it is moved to, or under gravity, forces and collisions
    enum class BodyMotion : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic
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
}