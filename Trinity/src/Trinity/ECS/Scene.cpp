#include "Trinity/trpch.h"

#include "Trinity/ECS/Scene.h"
#include "Trinity/ECS/Entity.h"
#include "Trinity/ECS/Components.h"

namespace Trinity
{
    Entity Scene::CreateEntity()
    {
        entt::entity l_Entity = m_Registry.create();

        Entity e(l_Entity, &m_Registry);
        e.AddComponent<TransformComponent>();

        return e;
    }

    Entity Scene::CreateLight(const glm::vec3& position, const glm::vec3& color)
    {
        Entity e = CreateEntity();
        auto& a_Light = e.AddComponent<LightComponent>();
        a_Light.Position = position;
        a_Light.Color = color;

        return e;
    }

    std::vector<LightComponent*> Scene::GetLights()
    {
        std::vector<LightComponent*> l_Lights;
        auto a_View = m_Registry.view<LightComponent>();
        for (auto it_Entity : a_View)
        {
            l_Lights.push_back(&a_View.get<LightComponent>(it_Entity));
        }

        return l_Lights;
    }
}