#pragma once

namespace Trinity
{
    // The prefab loader and the model's, registered with the asset manager once it is initialized and taken away before it shuts down
    namespace PrefabLoaders
    {
        void Register();
        void Unregister();
    }
}