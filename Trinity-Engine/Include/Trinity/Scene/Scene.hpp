#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <entt/entity/registry.hpp>

#include <cstddef>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Trinity
{
    class Entity;

    using SceneRegistry = entt::basic_registry<entt::entity, TaggedAllocator<entt::entity, MemoryTag::Scene>>;

    // Owns an EnTT registry whose pools and components allocate under Scene, and finds entities by UUID
    class TRINITY_API Scene
    {
    public:
        Scene();
        ~Scene();

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;
        Scene(Scene&&) = delete;
        Scene& operator=(Scene&&) = delete;

        // Adds an ID with a new UUID, and a Tag
        Entity CreateEntity(std::string_view name = "Entity");
        // Adds only the ID, for code that adds every other component itself, such as a scene loader. Returns an invalid entity if the UUID is invalid or taken
        Entity CreateEntityWithUUID(UUID uuid);
        void DestroyEntity(Entity entity);
        void Clear();

        [[nodiscard]] Entity FindEntityByUUID(UUID uuid);
        // The first entity in the Tag pool with this name
        [[nodiscard]] Entity FindEntityByName(std::string_view name);
        [[nodiscard]] std::size_t GetEntityCount() const;

        [[nodiscard]] SceneRegistry& GetRegistry() { return m_Registry; }
        [[nodiscard]] const SceneRegistry& GetRegistry() const { return m_Registry; }

    private:
        friend class Entity;

        using EntityMap = std::unordered_map<UUID, entt::entity, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, entt::entity>, MemoryTag::Scene>>;

        SceneRegistry m_Registry;
        EntityMap m_EntityMap;
    };
}