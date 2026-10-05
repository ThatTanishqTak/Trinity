#include "Trinity/Scene/Scene.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"

#include <vector>

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
        CreateStorage<IDComponent, TagComponent, TransformComponent, WorldTransformComponent, RelationshipComponent, CameraComponent, SpriteRendererComponent>(m_Registry);
    }

    Scene::~Scene() = default;

    Entity Scene::CreateEntity(std::string_view name)
    {
        return CreateEntity(name, Entity());
    }

    Entity Scene::CreateEntity(std::string_view name, Entity parent)
    {
        TR_CORE_ASSERT(!parent || parent.GetScene() == this, "Scene: CreateEntity was given a parent from another scene");

        UUID l_UUID = UUID::Generate();
        while (m_EntityMap.contains(l_UUID))
        {
            l_UUID = UUID::Generate();
        }

        Entity l_Entity = CreateEntityWithUUID(l_UUID);
        l_Entity.Add<TagComponent>().Tag = name;
        if (parent)
        {
            Unlink(l_Entity.GetHandle());
            Link(l_Entity.GetHandle(), parent.GetHandle(), entt::null);
        }

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
        l_Entity.Add<TransformComponent>();
        l_Entity.Add<WorldTransformComponent>();
        l_Entity.Add<RelationshipComponent>();
        Link(l_Entity.GetHandle(), entt::null, entt::null);
        m_EntityMap.emplace(uuid, l_Entity.GetHandle());

        return l_Entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        TR_CORE_ASSERT(entity.GetScene() == this && entity.IsValid(), "Scene: DestroyEntity was given an entity of another scene, or one already destroyed");

        const entt::entity l_Root = entity.GetHandle();

        std::vector<entt::entity, TaggedAllocator<entt::entity, MemoryTag::Scene>> l_Subtree;
        for (entt::entity it_Entity = l_Root; it_Entity != entt::null; it_Entity = GetNext(it_Entity, l_Root))
        {
            l_Subtree.push_back(it_Entity);
        }

        Unlink(l_Root);
        for (const entt::entity it_Entity : l_Subtree)
        {
            m_EntityMap.erase(m_Registry.get<IDComponent>(it_Entity).ID);
        }

        m_Registry.destroy(l_Subtree.begin(), l_Subtree.end());
    }

    Entity Scene::DuplicateEntity(Entity entity)
    {
        TR_CORE_ASSERT(entity.GetScene() == this && entity.IsValid(), "Scene: DuplicateEntity was given an entity of another scene, or one already destroyed");

        const RelationshipComponent& l_Relationship = m_Registry.get<RelationshipComponent>(entity.GetHandle());

        return Entity(Duplicate(entity.GetHandle(), l_Relationship.Parent, l_Relationship.NextSibling), this);
    }

    void Scene::Clear()
    {
        m_Registry.clear();
        m_EntityMap.clear();
        m_FirstRoot = entt::null;
        m_LastRoot = entt::null;
        m_RootCount = 0;
    }

    bool Scene::SetParent(Entity entity, Entity parent, bool keepWorldTransform)
    {
        TR_CORE_ASSERT(!parent || parent.GetScene() == this, "Scene: SetParent was given a parent from another scene");

        return Move(entity, parent ? parent.GetHandle() : entt::null, entt::null, keepWorldTransform);
    }

    bool Scene::MoveBefore(Entity entity, Entity sibling, bool keepWorldTransform)
    {
        TR_CORE_ASSERT(sibling.GetScene() == this && sibling.IsValid(), "Scene: MoveBefore was given a sibling of another scene, or one already destroyed");

        if (sibling == entity)
        {
            return true;
        }

        return Move(entity, m_Registry.get<RelationshipComponent>(sibling.GetHandle()).Parent, sibling.GetHandle(), keepWorldTransform);
    }

    void Scene::UpdateWorldTransforms()
    {
        TR_PROFILE_FUNCTION();

        for (entt::entity it_Entity = m_FirstRoot; it_Entity != entt::null; it_Entity = GetNext(it_Entity, entt::null))
        {
            const entt::entity l_Parent = m_Registry.get<RelationshipComponent>(it_Entity).Parent;
            const glm::mat4 l_Local = m_Registry.get<TransformComponent>(it_Entity).GetMatrix();

            m_Registry.get<WorldTransformComponent>(it_Entity).Matrix = l_Parent == entt::null ? l_Local : m_Registry.get<WorldTransformComponent>(l_Parent).Matrix * l_Local;
        }
    }

    glm::mat4 Scene::ComputeWorldMatrix(Entity entity) const
    {
        TR_CORE_ASSERT(entity.GetScene() == this && entity.IsValid(), "Scene: ComputeWorldMatrix was given an entity of another scene, or one already destroyed");

        return ComputeWorld(entity.GetHandle());
    }

    Entity Scene::GetFirstRoot()
    {
        return Entity(m_FirstRoot, this);
    }

    Entity Scene::GetNextInHierarchyOrder(Entity entity)
    {
        return Entity(GetNext(entity.GetHandle(), entt::null), this);
    }

    Entity Scene::GetNextInSubtree(Entity entity, Entity root)
    {
        return Entity(GetNext(entity.GetHandle(), root.GetHandle()), this);
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

    // Down to the first child, else along to the next sibling, else up until an ancestor has one, never leaving the subtree under root
    entt::entity Scene::GetNext(entt::entity entity, entt::entity root) const
    {
        const RelationshipComponent& l_Relationship = m_Registry.get<RelationshipComponent>(entity);
        if (l_Relationship.FirstChild != entt::null)
        {
            return l_Relationship.FirstChild;
        }

        for (entt::entity it_Entity = entity; it_Entity != root && it_Entity != entt::null; it_Entity = m_Registry.get<RelationshipComponent>(it_Entity).Parent)
        {
            const entt::entity l_Next = m_Registry.get<RelationshipComponent>(it_Entity).NextSibling;
            if (l_Next != entt::null)
            {
                return l_Next;
            }
        }

        return entt::null;
    }

    bool Scene::IsSelfOrAncestor(entt::entity entity, entt::entity descendant) const
    {
        for (entt::entity it_Entity = descendant; it_Entity != entt::null; it_Entity = m_Registry.get<RelationshipComponent>(it_Entity).Parent)
        {
            if (it_Entity == entity)
            {
                return true;
            }
        }

        return false;
    }

    glm::mat4 Scene::ComputeWorld(entt::entity entity) const
    {
        glm::mat4 l_World = m_Registry.get<TransformComponent>(entity).GetMatrix();
        for (entt::entity it_Parent = m_Registry.get<RelationshipComponent>(entity).Parent; it_Parent != entt::null; it_Parent = m_Registry.get<RelationshipComponent>(it_Parent).Parent)
        {
            l_World = m_Registry.get<TransformComponent>(it_Parent).GetMatrix() * l_World;
        }

        return l_World;
    }

    bool Scene::Move(Entity entity, entt::entity parent, entt::entity before, bool keepWorldTransform)
    {
        TR_CORE_ASSERT(entity.GetScene() == this && entity.IsValid(), "Scene: an entity of another scene, or one already destroyed, cannot be moved");

        const entt::entity l_Entity = entity.GetHandle();
        if (parent != entt::null && IsSelfOrAncestor(l_Entity, parent))
        {
            return false;
        }

        const glm::mat4 l_World = keepWorldTransform ? ComputeWorld(l_Entity) : glm::mat4(1.0f);

        Unlink(l_Entity);
        Link(l_Entity, parent, before);

        if (keepWorldTransform)
        {
            const glm::mat4 l_ParentWorld = parent != entt::null ? ComputeWorld(parent) : glm::mat4(1.0f);
            m_Registry.get<TransformComponent>(l_Entity).SetMatrix(glm::inverse(l_ParentWorld) * l_World);
        }

        return true;
    }

    // Inserts before a sibling, or at the end when before is null, under a parent or among the roots when parent is null
    void Scene::Link(entt::entity entity, entt::entity parent, entt::entity before)
    {
        RelationshipComponent& l_Relationship = m_Registry.get<RelationshipComponent>(entity);
        RelationshipComponent* l_Parent = parent != entt::null ? &m_Registry.get<RelationshipComponent>(parent) : nullptr;
        entt::entity& l_First = l_Parent != nullptr ? l_Parent->FirstChild : m_FirstRoot;
        entt::entity& l_Last = l_Parent != nullptr ? l_Parent->LastChild : m_LastRoot;

        l_Relationship.Parent = parent;
        l_Relationship.NextSibling = before;
        l_Relationship.PreviousSibling = before != entt::null ? m_Registry.get<RelationshipComponent>(before).PreviousSibling : l_Last;

        if (l_Relationship.PreviousSibling != entt::null)
        {
            m_Registry.get<RelationshipComponent>(l_Relationship.PreviousSibling).NextSibling = entity;
        }
        else
        {
            l_First = entity;
        }

        if (before != entt::null)
        {
            m_Registry.get<RelationshipComponent>(before).PreviousSibling = entity;
        }
        else
        {
            l_Last = entity;
        }

        ++(l_Parent != nullptr ? l_Parent->ChildCount : m_RootCount);
    }

    void Scene::Unlink(entt::entity entity)
    {
        RelationshipComponent& l_Relationship = m_Registry.get<RelationshipComponent>(entity);
        RelationshipComponent* l_Parent = l_Relationship.Parent != entt::null ? &m_Registry.get<RelationshipComponent>(l_Relationship.Parent) : nullptr;

        if (l_Relationship.PreviousSibling != entt::null)
        {
            m_Registry.get<RelationshipComponent>(l_Relationship.PreviousSibling).NextSibling = l_Relationship.NextSibling;
        }
        else
        {
            (l_Parent != nullptr ? l_Parent->FirstChild : m_FirstRoot) = l_Relationship.NextSibling;
        }

        if (l_Relationship.NextSibling != entt::null)
        {
            m_Registry.get<RelationshipComponent>(l_Relationship.NextSibling).PreviousSibling = l_Relationship.PreviousSibling;
        }
        else
        {
            (l_Parent != nullptr ? l_Parent->LastChild : m_LastRoot) = l_Relationship.PreviousSibling;
        }

        --(l_Parent != nullptr ? l_Parent->ChildCount : m_RootCount);

        l_Relationship.Parent = entt::null;
        l_Relationship.PreviousSibling = entt::null;
        l_Relationship.NextSibling = entt::null;
    }

    // Every pool the source is in is copied through the pool's own type-erased copy, so components added by modules come along too. The ID and the links are the copy's own
    entt::entity Scene::Duplicate(entt::entity source, entt::entity parent, entt::entity before)
    {
        UUID l_UUID = UUID::Generate();
        while (m_EntityMap.contains(l_UUID))
        {
            l_UUID = UUID::Generate();
        }

        const entt::entity l_Copy = CreateEntityWithUUID(l_UUID).GetHandle();
        for (auto [it_Id, it_Storage] : m_Registry.storage())
        {
            if (it_Id == entt::type_hash<IDComponent>::value() || it_Id == entt::type_hash<RelationshipComponent>::value() || !it_Storage.contains(source))
            {
                continue;
            }

            if (it_Storage.contains(l_Copy))
            {
                it_Storage.erase(l_Copy);
            }

            it_Storage.push(l_Copy, it_Storage.value(source));
        }

        Unlink(l_Copy);
        Link(l_Copy, parent, before);

        for (entt::entity it_Child = m_Registry.get<RelationshipComponent>(source).FirstChild; it_Child != entt::null; it_Child = m_Registry.get<RelationshipComponent>(it_Child).NextSibling)
        {
            Duplicate(it_Child, l_Copy, entt::null);
        }

        return l_Copy;
    }
}