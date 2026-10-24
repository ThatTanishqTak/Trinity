#pragma once

#include "Trinity/Asset/Asset.hpp"
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
        // A .trprefab holds one entity and its subtree, each under a local ID that stays the same through every edit of the prefab, written as scene format 2 writes entities. Format 2 added instances of other prefabs in it, nested, or as its root for a variant, so a format 1 prefab reads as it is and is written as format 2
        constexpr std::uint32_t c_FormatVersion = 2;
        constexpr std::string_view c_Extension = ".trprefab";

        // An instance's entity's UUID, made from the instance's UUID and the entity's local ID, so it is the same on every load and no two instances share one. For one instance a UUID tells back its local ID, which is how a hierarchy made into a prefab keeps its UUIDs as the prefab's first instance. An instance nested in a prefab has a local ID of its own, so in an instance of that prefab it has the UUID made from them, and its entities' are made from that
        [[nodiscard]] TRINITY_API UUID MakeEntityUUID(UUID instance, UUID local);
        [[nodiscard]] TRINITY_API UUID FindLocalID(UUID instance, UUID entity);

        // The UUID an entity of a prefab's template has in an instance of the prefab: one of the prefab's own makes it from the instance's UUID and its local ID, and one of an instance nested in the prefab is given it by that nested instance, under the UUID the nested instance has in this one, so UUIDs are made through every level and a variant's are its base's. Main thread only
        [[nodiscard]] TRINITY_API UUID GetInstanceEntityUUID(UUID instance, Entity member);
        // The entity of the prefab's template that an instance's entity is, from its UUID, or a null entity for one the instance has no such entity for. Main thread only
        [[nodiscard]] TRINITY_API Entity FindTemplateEntity(Scene& prefab, UUID instance, UUID entity);

        // The root of the instance whose prefab has the entity, the entity itself for an instance's root, missing prefab or not, and a null entity for one of no instance, as one added under an instance is, or one under an instance whose prefab the resolver does not find. In a scene the outermost instance is the one, since an instance nested in its prefab has no root of its own there. In a prefab's template it is the instance nested in it. Main thread only
        [[nodiscard]] TRINITY_API Entity FindInstanceRoot(Entity entity);

        // One prefab an instance's entity is part of: the prefab, the UUID of the instance of it that the entity is in, the instance's root in the entity's scene, a variant's shared with its base, or null where the outer instance removed it, and the entity's own in that prefab's template
        struct Level
        {
            UUID Prefab;
            UUID Instance;
            Entity Root;
            Entity Template;
        };

        // Outermost first: the scene's instance, then each instance nested in its prefab that the entity is part of, down to the innermost, as a Weapon in an Enemy gives the Enemy and then the Weapon. None for an entity of no instance, or one whose prefab is missing. Main thread only
        [[nodiscard]] TRINITY_API std::vector<Level> GetLevels(Entity entity);
        // The same from the entity of the outermost prefab's template that the instance has, or would have, as one the instance removed
        [[nodiscard]] TRINITY_API std::vector<Level> GetLevels(Entity root, Entity member);

        // Whether the prefab holds an instance of the other, at any depth, nested or as a variant's base, so placing an instance of the other in it would make it hold itself. A prefab holds itself. Main thread only
        [[nodiscard]] TRINITY_API bool Holds(UUID prefab, UUID other);

        // What an instance's root would be, and each instance nested in its prefab one level down, as Unity's Unpack leaves them: each nested instance's root in the scene with the component it would have, its UUID made as an entity's is. A variant's root is the root of its base's instance. A nested instance the instance removed is left out, and so is one whose prefab is missing, which stays a plain entity standing in for it. Main thread only
        [[nodiscard]] TRINITY_API std::vector<std::pair<Entity, PrefabInstanceComponent>> GetNestedInstances(Entity root);
    }

    // A prefab as instancing reads it: its entities under their local IDs, in a scene of their own that the loader makes on the main thread, where scenes are made. One that holds instances of other prefabs is made once they are loaded, as a scene waits for its prefabs before it is read
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

        // Made on first use once every prefab nested in it has loaded, or failed to, and null until then. Instancing only reads it. Main thread only
        [[nodiscard]] Scene* GetTemplate() const;
        // The prefabs its instances are of, each once, which a scene holds while it holds this one
        [[nodiscard]] std::span<const UUID> GetNestedPrefabs() const { return m_Nested; }
        // Whether every prefab nested in it, and in each of them, has loaded or failed to, so its template can be made. One not asked for yet is waited for, as the scene manager asks for each before it reads a scene
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

    // Reads .trprefab files, which the asset registry imports as Prefab. A prefab that cannot be read fails to load, and its instances are kept as their text
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
}