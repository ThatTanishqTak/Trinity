#include "Trinity/Scene/Scene.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"

namespace Trinity
{
    namespace
    {
        // Created inside the engine, so a pool, and the code that destroys it, never belongs to a module that adds the component first and then unloads
        template<Component... T>
        void CreateStorage(SceneRegistry& registry)
        {
            (static_cast<void>(registry.storage<T>()), ...);
        }
    }

    Scene::Scene()
    {
        CreateStorage<IDComponent, TagComponent>(m_Registry);
    }

    Scene::~Scene() = default;

    Entity Scene::CreateEntity(std::string_view name)
    {
        UUID l_UUID = UUID::Generate();
        while (m_EntityMap.contains(l_UUID))
        {
            l_UUID = UUID::Generate();
        }

        Entity l_Entity = CreateEntityWithUUID(l_UUID);
        l_Entity.Add<TagComponent>().Tag = name;

        return l_Entity;
    }

    Entity Scene::CreateEntityWithUUID(UUID uuid)
    {
        if (!uuid.IsValid() || m_EntityMap.contains(uuid))
        {
            TR_CORE_ERROR("Scene: cannot create an entity with UUID {}, because it is {}", uuid, uuid.IsValid() ? "taken" : "invalid");

            return {};
        }

        Entity l_Entity(m_Registry.create(), this);
        l_Entity.Add<IDComponent>(uuid);
        m_EntityMap.emplace(uuid, l_Entity.GetHandle());

        return l_Entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        TR_CORE_ASSERT(entity.GetScene() == this && entity.IsValid(), "Scene: DestroyEntity was given an entity of another scene, or one already destroyed");

        m_EntityMap.erase(entity.GetUUID());
        m_Registry.destroy(entity.GetHandle());
    }

    void Scene::Clear()
    {
        m_Registry.clear();
        m_EntityMap.clear();
    }

    Entity Scene::FindEntityByUUID(UUID uuid)
    {
        const auto a_Found = m_EntityMap.find(uuid);

        return a_Found != m_EntityMap.end() ? Entity(a_Found->second, this) : Entity();
    }

    Entity Scene::FindEntityByName(std::string_view name)
    {
        for (const auto [it_Handle, it_Tag] : m_Registry.view<TagComponent>().each())
        {
            if (std::string_view(it_Tag.Tag) == name)
            {
                return Entity(it_Handle, this);
            }
        }

        return {};
    }

    std::size_t Scene::GetEntityCount() const
    {
        return m_EntityMap.size();
    }
}