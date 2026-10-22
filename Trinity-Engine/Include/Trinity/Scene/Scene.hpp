#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <entt/entity/registry.hpp>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Trinity
{
    class Entity;
    struct TransformComponent;

    using SceneRegistry = entt::basic_registry<entt::entity, TaggedAllocator<entt::entity, MemoryTag::Scene>>;

    // Owns an EnTT registry whose pools and components allocate under Scene, finds entities by UUID, and keeps them in a hierarchy whose roots are ordered too
    class TRINITY_API Scene
    {
    public:
        Scene();
        ~Scene();

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;
        Scene(Scene&&) = delete;
        Scene& operator=(Scene&&) = delete;

        Entity CreateEntity(std::string_view name = "Entity");
        Entity CreateEntity(std::string_view name, Entity parent);
        Entity CreateEntityWithUUID(UUID uuid);
        void DestroyEntity(Entity entity);
        Entity DuplicateEntity(Entity entity);
        void Clear();

        // Each entity a component of the entity or of its subtree names, a joint's connected body, a script's entity field or a module component's, each of a list of them too, given to the function, which may change it
        void RemapEntityReferences(Entity root, const std::function<void(UUID& entity)>& remap);

        bool SetParent(Entity entity, Entity parent, bool keepWorldTransform = true);
        bool MoveBefore(Entity entity, Entity sibling, bool keepWorldTransform = true);

        void UpdateWorldTransforms();
        [[nodiscard]] glm::mat4 ComputeWorldMatrix(Entity entity) const;
        // The entity's place in the world, from its transform and its parents', with any shear dropped
        [[nodiscard]] TransformComponent ComputeWorldTransform(Entity entity) const;
        // Puts the entity at a place in the world by setting its transform relative to its parent, as Entity::Replace does, so a running scene's physics teleports its body
        void SetWorldTransform(Entity entity, const TransformComponent& world);

        [[nodiscard]] Entity GetFirstRoot();
        [[nodiscard]] Entity GetNextInHierarchyOrder(Entity entity);
        [[nodiscard]] Entity GetNextInSubtree(Entity entity, Entity root);
        [[nodiscard]] std::uint32_t GetRootCount() const { return m_RootCount; }

        [[nodiscard]] Entity FindEntityByUUID(UUID uuid);
        [[nodiscard]] Entity FindEntityByName(std::string_view name);
        [[nodiscard]] std::size_t GetEntityCount() const;

        [[nodiscard]] SceneRegistry& GetRegistry() { return m_Registry; }
        [[nodiscard]] const SceneRegistry& GetRegistry() const { return m_Registry; }

    private:
        friend class Entity;

        using EntityMap = std::unordered_map<UUID, entt::entity, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, entt::entity>, MemoryTag::Scene>>;
        // The UUID each entity a duplicate copies is to have where it is not a new random one, and those UUIDs, and the new instance UUID of each prefab instance root it copies, or none for one copied as a plain entity
        struct CopyPlan
        {
            std::unordered_map<entt::entity, UUID> Entities;
            std::unordered_set<UUID> Taken;
            std::unordered_map<entt::entity, UUID> Instances;
        };

        [[nodiscard]] entt::entity GetNext(entt::entity entity, entt::entity root) const;
        [[nodiscard]] bool IsSelfOrAncestor(entt::entity entity, entt::entity descendant) const;
        [[nodiscard]] glm::mat4 ComputeWorld(entt::entity entity) const;
        bool Move(Entity entity, entt::entity parent, entt::entity before, bool keepWorldTransform);
        void Link(entt::entity entity, entt::entity parent, entt::entity before);
        void Unlink(entt::entity entity);
        [[nodiscard]] CopyPlan PlanInstanceCopies(entt::entity source) const;
        entt::entity Duplicate(entt::entity source, entt::entity parent, entt::entity before, const CopyPlan& plan);

        SceneRegistry m_Registry;
        EntityMap m_EntityMap;
        entt::entity m_FirstRoot = entt::null;
        entt::entity m_LastRoot = entt::null;
        std::uint32_t m_RootCount = 0;
    };
}