#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Asset/ModelData.hpp"
#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include "Trinity/Scene/Entity.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace Prefab
    {
        // One entity and its subtree, each under a local ID that survives edits. Format 2 added nesting
        constexpr std::uint32_t c_FormatVersion = 2;
        constexpr std::string_view c_Extension = ".trprefab";

        // Made from the instance's UUID and the entity's local ID, so it is the same on every load
        [[nodiscard]] TRINITY_API UUID MakeEntityUUID(UUID instance, UUID local);
        [[nodiscard]] TRINITY_API UUID FindLocalID(UUID instance, UUID entity);

        // Made through every level of nesting, and a variant's are its base's. Main thread only
        [[nodiscard]] TRINITY_API UUID GetInstanceEntityUUID(UUID instance, Entity member);
        // A null entity when the instance has no such entity. Main thread only
        [[nodiscard]] TRINITY_API Entity FindTemplateEntity(Scene& prefab, UUID instance, UUID entity);

        // In a scene the outermost instance's root. Null for an entity of no instance. Main thread only
        [[nodiscard]] TRINITY_API Entity FindInstanceRoot(Entity entity);

        // One prefab an instance's entity is part of. Root is null where the outer instance removed it
        struct Level
        {
            UUID Prefab;
            UUID Instance;
            Entity Root;
            Entity Template;
        };

        // Outermost first, as a Weapon in an Enemy gives the Enemy and then the Weapon. Main thread only
        [[nodiscard]] TRINITY_API std::vector<Level> GetLevels(Entity entity);
        // The same, from the entity of the outermost prefab's template
        [[nodiscard]] TRINITY_API std::vector<Level> GetLevels(Entity root, Entity member);

        // At any depth, nested or as a variant's base. A prefab holds itself. Main thread only
        [[nodiscard]] TRINITY_API bool Holds(UUID prefab, UUID other);

        // As Unity's Unpack: the root and each nested instance one level down. Main thread only
        [[nodiscard]] TRINITY_API std::vector<std::pair<Entity, PrefabInstanceComponent>> GetNestedInstances(Entity root);
    }

    // Its entities under their local IDs, in a scene of their own made on the main thread
    class TRINITY_API PrefabAsset final : public Asset
    {
    public:
        static constexpr std::string_view c_AssetType = "Prefab";

        explicit PrefabAsset(std::string_view text);
        ~PrefabAsset() override;

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_AssetType;
        }

        // Made on first use once every nested prefab has loaded or failed, and null until then
        [[nodiscard]] Scene* GetTemplate() const;
        // The prefabs its instances are of, each once, which a scene holds while it holds this one
        [[nodiscard]] std::span<const UUID> GetNestedPrefabs() const { return m_Nested; }
        // Whether every prefab nested in it, at any depth, has loaded or failed to
        [[nodiscard]] bool IsComplete() const;

    private:
        friend class PrefabLoader;

        void MakeTemplate() const;

        mutable TaggedString<MemoryTag::Assets> m_Text;
        mutable Scope<Scene> m_Template;
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Assets>> m_Nested;
        mutable bool m_Checking = false;
        mutable bool m_Making = false;
    };

    // A prefab that cannot be read fails to load, and its instances are kept as their text
    class TRINITY_API PrefabLoader final : public AssetLoader
    {
    public:
        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return PrefabAsset::c_AssetType;
        }

        [[nodiscard]] Expected<Asset*, std::string> Load(std::span<const std::byte> data) const override;
        [[nodiscard]] Expected<void, std::string> Finish(Asset& asset) const override;
    };

    // A model's UUID loads as a PrefabAsset, from the hierarchy its import cooked
    class TRINITY_API ModelPrefabLoader final : public AssetLoader
    {
    public:
        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_ModelAssetType;
        }

        [[nodiscard]] std::string GetLoadPath(const AssetRecord& record) const override;
        [[nodiscard]] Expected<Asset*, std::string> Load(std::span<const std::byte> data) const override;
        [[nodiscard]] Expected<void, std::string> Finish(Asset& asset) const override;

    private:
        PrefabLoader m_Prefabs;
    };
}