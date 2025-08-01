#pragma once

#include <entt/entt.hpp>

namespace Trinity
{
    class Entity;

    class Scene
    {
    public:
        Scene() = default;
        ~Scene() = default;

        Entity CreateEntity();

        entt::registry& GetRegistry() { return m_Registry; }

    private:
        entt::registry m_Registry;
    };
}