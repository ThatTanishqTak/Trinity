#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Trinity
{
    class Entity;
    class Scene;

    namespace Prefab
    {
        // A .trprefab holds one entity and its subtree, each under a local ID that stays the same through every edit of the prefab, written as scene format 2 writes entities
        constexpr std::uint32_t c_FormatVersion = 1;
        constexpr std::string_view c_Extension = ".trprefab";

        // An instance's entity's UUID, made from the instance's UUID and the entity's local ID, so it is the same on every load and no two instances share one. For one instance a UUID tells back its local ID, which is how a hierarchy made into a prefab keeps its UUIDs as the prefab's first instance
        [[nodiscard]] TRINITY_API UUID MakeEntityUUID(UUID instance, UUID local);
        [[nodiscard]] TRINITY_API UUID FindLocalID(UUID instance, UUID entity);

        // The root of the instance whose prefab has the entity, the entity itself for an instance's root, missing prefab or not, and a null entity for one of no instance, as one added under an instance is, or one under an instance whose prefab the resolver does not find. Main thread only
        [[nodiscard]] TRINITY_API Entity FindInstanceRoot(Entity entity);
    }

    // A prefab as instancing reads it: its entities under their local IDs, in a scene of their own that the loader makes on the main thread, where scenes are made
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

        // Null until the loader has finished it. Instancing only reads it
        [[nodiscard]] Scene* GetTemplate() const { return m_Template.get(); }

    private:
        friend class PrefabLoader;

        TaggedString<MemoryTag::Assets> m_Text;
        Scope<Scene> m_Template;
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