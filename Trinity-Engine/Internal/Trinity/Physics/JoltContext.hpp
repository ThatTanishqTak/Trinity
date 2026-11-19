#pragma once

// Included only by sources in Source/Trinity/Physics, which are built for Jolt's instructions
#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystem.h>

namespace Trinity
{
    // The first world acquires it, the last releases it, so Physics is 0 B with no world. Main thread
    namespace JoltContext
    {
        void Acquire();
        void Release();

        // What cooking or restoring a shape needs. Held by every world and loading shape, from any thread
        void AcquireTypes();
        void ReleaseTypes();

        [[nodiscard]] JPH::JobSystem& GetJobSystem();
    }
}