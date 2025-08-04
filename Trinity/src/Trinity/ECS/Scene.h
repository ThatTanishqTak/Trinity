#pragma once

#include <entt/entt.hpp>
#include <vector>
#include <glm/glm.hpp>

#include "Trinity/ECS/Components.h"

namespace Trinity
{
    class Entity;

    class Scene
    {
    public:
        Scene() = default;
        ~Scene() = default;

        Entity CreateEntity();
        Entity CreateLight(const glm::vec3& position, const glm::vec3& color);

        std::vector<Light*> GetLights();

        entt::registry& GetRegistry() { return m_Registry; }

    private:
        entt::registry m_Registry;
    };
}