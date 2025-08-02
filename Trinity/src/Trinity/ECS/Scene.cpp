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
}