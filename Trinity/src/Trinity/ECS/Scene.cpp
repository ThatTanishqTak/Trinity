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
        e.AddComponent<Transform>();

        return e;
    }

    Entity Scene::CreateLight(const glm::vec3& position, const glm::vec3& color)
    {
        Entity e = CreateEntity();
        auto& a_Light = e.AddComponent<Light>();
        a_Light.Position = position;
        a_Light.Color = color;

        return e;
    }

    std::vector<Light*> Scene::GetLights()
    {
        std::vector<Light*> l_Lights;
        auto a_View = m_Registry.view<Light>();
        for (auto it_Entity : a_View)
        {
            l_Lights.push_back(&a_View.get<Light>(it_Entity));
        }

        return l_Lights;
    }
}