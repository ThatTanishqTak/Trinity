#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <vector>

namespace Trinity
{
    class AssetRegistry;
    class Scene;

    // What a scene names of the project's assets, found the same way where a scene is loaded and where a build is gathered, so a game ships every asset its scenes hold
    namespace SceneAssets
    {
        using AssetList = std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scene>>;

        // Once, in the order met, and never an invalid UUID
        TRINITY_API void Add(AssetList& assets, UUID id);
        // What the scene's components and its scripts' fields name, which the renderer, physics and scripts would otherwise load as they first reach them. A field naming a scene is left out, as a scene is loaded by the scene manager and not the asset manager
        TRINITY_API void Gather(Scene& scene, const AssetRegistry* registry, AssetList& assets);
        // The prefabs the instances of a scene already read are of, as its components name them. A stand-in names one that could not be found, so it is left out
        TRINITY_API void GatherInstancePrefabs(Scene& scene, AssetList& assets);
    }
}