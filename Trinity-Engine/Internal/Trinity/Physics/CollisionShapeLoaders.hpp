#pragma once

namespace Trinity
{
    // The loaders of convex hulls and collision meshes, registered with the asset manager once it is initialized and taken away before it shuts down. Nothing of Jolt's is made until a shape loads
    namespace CollisionShapeLoaders
    {
        void Register();
        void Unregister();
    }
}