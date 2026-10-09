#pragma once

#include <cstdint>

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
}