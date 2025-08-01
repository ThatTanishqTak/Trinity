#include "Trinity/trpch.h"

#include "Trinity/ECS/Entity.h"

namespace Trinity
{
    Entity::Entity(entt::entity entity, entt::registry* registry) : m_Entity(entity), m_Registry(registry)
    {

    }

    void Entity::Destroy()
    {
        if (m_Registry)
        {
            m_Registry->destroy(m_Entity);
        }
    }
}