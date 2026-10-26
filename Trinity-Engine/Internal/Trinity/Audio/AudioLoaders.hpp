#pragma once

namespace Trinity
{
    // The audio clip and mixer loaders, registered with the asset manager once it is initialized and taken away before it shuts down. Taking them away waits for the clips still loading in the background
    namespace AudioLoaders
    {
        void Register();
        void Unregister();
    }
}