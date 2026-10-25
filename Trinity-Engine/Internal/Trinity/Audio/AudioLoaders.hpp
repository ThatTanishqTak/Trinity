#pragma once

namespace Trinity
{
    // The audio clip loader, registered with the asset manager once it is initialized and taken away before it shuts down. Taking it away waits for the clips still loading in the background
    namespace AudioLoaders
    {
        void Register();
        void Unregister();
    }
}