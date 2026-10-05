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
#include <utility>

namespace Trinity
{
    class Entity;

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

        bool SetParent(Entity entity, Entity parent, bool keepWorldTransform = true);
        bool MoveBefore(Entity entity, Entity sibling, bool keepWorldTransform = true);

        void UpdateWorldTransforms();
        [[nodiscard]] glm::mat4 ComputeWorldMatrix(Entity entity) const;

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

        [[nodiscard]] entt::entity GetNext(entt::entity entity, entt::entity root) const;
        [[nodiscard]] bool IsSelfOrAncestor(entt::entity entity, entt::entity descendant) const;
        [[nodiscard]] glm::mat4 ComputeWorld(entt::entity entity) const;
        bool Move(Entity entity, entt::entity parent, entt::entity before, bool keepWorldTransform);
        void Link(entt::entity entity, entt::entity parent, entt::entity before);
        void Unlink(entt::entity entity);
        entt::entity Duplicate(entt::entity source, entt::entity parent, entt::entity before);

        SceneRegistry m_Registry;
        EntityMap m_EntityMap;
        entt::entity m_FirstRoot = entt::null;
        entt::entity m_LastRoot = entt::null;
        std::uint32_t m_RootCount = 0;
    };
}