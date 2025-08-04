#include "Trinity/trpch.h"

#include "Trinity/ECS/Scene.h"
#include "Trinity/ECS/Entity.h"
#include "Trinity/ECS/Components.h"

namespace Trinity
{
    Entity Scene::CreateEntity()
    {
        entt::entity entity = m_Registry.create();

        Entity e(entity, &m_Registry);
        e.AddComponent<Transform>();

        return e;
    }

    Entity Scene::CreateLight(const glm::vec3& position, const glm::vec3& color)
    {
        Entity e = CreateEntity();
        auto& light = e.AddComponent<Light>();
        light.Position = position;
        light.Color = color;
        return e;
    }

    std::vector<Light*> Scene::GetLights()
    {
        std::vector<Light*> lights;
        auto view = m_Registry.view<Light>();
        for (auto entity : view)
        {
            lights.push_back(&view.get<Light>(entity));
        }
        return lights;
    }
}