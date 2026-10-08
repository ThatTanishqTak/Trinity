#pragma once

#include <entt/entity/entity.hpp>

#include <cstdint>

namespace Trinity
{
    // An entity as the ID targets hold it: its handle plus one, so the 0 they are cleared to stands for entt::null, no entity
    [[nodiscard]] constexpr std::uint32_t ToPickID(entt::entity entity)
    {
        return static_cast<std::uint32_t>(entity) + 1u;
    }

    [[nodiscard]] constexpr entt::entity FromPickID(std::uint32_t id)
    {
        return static_cast<entt::entity>(id - 1u);
    }
}