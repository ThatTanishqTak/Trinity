#pragma once

namespace Trinity
{
    // Registered once the asset manager is initialized. Removing them waits for clips still loading
    namespace AudioLoaders
    {
        void Register();
        void Unregister();
    }
}