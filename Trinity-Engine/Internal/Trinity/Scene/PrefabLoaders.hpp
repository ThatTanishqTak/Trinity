#pragma once

namespace Trinity
{
    // Registered once the asset manager is initialized, and removed before it shuts down
    namespace PrefabLoaders
    {
        void Register();
        void Unregister();
    }
}