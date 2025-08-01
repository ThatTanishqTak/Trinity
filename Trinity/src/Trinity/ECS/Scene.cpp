#include "Trinity/trpch.h"

#include "Trinity/ECS/Scene.h"
#include "Trinity/ECS/Entity.h"

namespace Trinity
{
    Entity Scene::CreateEntity()
    {
        entt::entity entity = m_Registry.create();
        return Entity(entity, &m_Registry);
    }
}