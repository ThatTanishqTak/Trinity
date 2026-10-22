#pragma once

#include "Trinity/Core/UUID.hpp"

namespace Trinity
{
    class Scene;

    namespace SceneSerializer
    {
        // Every scene there is, which Scene adds as it is made and takes out as it is destroyed, so a component registered later gets its pool in each and one unregistered is kept as YAML in each. Nothing before the serializer is initialized or after it shuts down. Main thread only
        void AttachScene(Scene& scene);
        void DetachScene(Scene& scene);

        // The prefab's entities under their local IDs, as the prefab resolver finds them, or null for a prefab that is not there or cannot be read. Main thread only
        [[nodiscard]] Scene* FindPrefabTemplate(UUID prefab);
    }
}