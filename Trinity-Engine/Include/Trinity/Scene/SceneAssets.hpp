#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <vector>

namespace Trinity
{
    class AssetRegistry;
    class Scene;

    // Found the same way where a scene loads and where a build gathers, so a game ships them all
    namespace SceneAssets
    {
        using AssetList = std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scene>>;

        // Once, in the order met, and never an invalid UUID
        TRINITY_API void Add(AssetList& assets, UUID id);
        // Fields naming a scene or a UI file are left out: the scene manager and GameUI read those
        TRINITY_API void Gather(Scene& scene, const AssetRegistry* registry, AssetList& assets);
        // A stand-in names a prefab that could not be found, so it is left out
        TRINITY_API void GatherInstancePrefabs(Scene& scene, AssetList& assets);
        // UI files are read by GameUI by path, never held by the asset manager. A build ships them
        TRINITY_API void GatherDocuments(Scene& scene, const AssetRegistry* registry, AssetList& assets);
    }
}