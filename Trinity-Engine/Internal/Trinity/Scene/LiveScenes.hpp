#pragma once

namespace Trinity
{
    class Scene;

    namespace SceneSerializer
    {
        // Every scene there is, which Scene adds as it is made and takes out as it is destroyed, so a component registered later gets its pool in each and one unregistered is kept as YAML in each. Nothing before the serializer is initialized or after it shuts down. Main thread only
        void AttachScene(Scene& scene);
        void DetachScene(Scene& scene);
    }
}