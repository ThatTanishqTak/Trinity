#pragma once

namespace Trinity
{
    class Scene;

    namespace SceneSerializer
    {
        // Every scene alive, so a component registered later gets its pool in each. Main thread only
        void AttachScene(Scene& scene);
        void DetachScene(Scene& scene);
    }
}