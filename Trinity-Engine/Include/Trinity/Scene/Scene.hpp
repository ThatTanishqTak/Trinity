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

    // Pools and components allocate under Scene. The hierarchy's roots are ordered too
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

        // Calls the function with each entity a component in the subtree names. It may change it
        void RemapEntityReferences(Entity root, const std::function<void(UUID& entity)>& remap);

        bool SetParent(Entity entity, Entity parent, bool keepWorldTransform = true);
        bool MoveBefore(Entity entity, Entity sibling, bool keepWorldTransform = true);

        void UpdateWorldTransforms();
        [[nodiscard]] glm::mat4 ComputeWorldMatrix(Entity entity) const;
        // From its transform and its parents', with any shear dropped
        [[nodiscard]] TransformComponent ComputeWorldTransform(Entity entity) const;
        // As drawn: running bodies are at their pose between the last two steps. Read in OnLateUpdate
        [[nodiscard]] TransformComponent ComputeDrawnTransform(Entity entity) const;
        // Sets the transform relative to its parent, as Entity::Replace does, so physics teleports it
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
        // The UUIDs a duplicate's entities take, and the new instance UUID of each instance root copied
        struct CopyPlan
        {
            std::unordered_map<entt::entity, UUID> Entities;
            std::unordered_set<UUID> Taken;
            std::unordered_map<entt::entity, UUID> Instances;
        };

        [[nodiscard]] entt::entity GetNext(entt::entity entity, entt::entity root) const;
        [[nodiscard]] bool IsSelfOrAncestor(entt::entity entity, entt::entity descendant) const;
        [[nodiscard]] glm::mat4 ComputeWorld(entt::entity entity) const;
        [[nodiscard]] glm::mat4 ComputeDrawn(entt::entity entity) const;
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