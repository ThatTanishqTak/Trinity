#pragma once

// Included only by sources in Source/Trinity/Physics, which are built for Jolt's instructions
#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystem.h>

namespace Trinity
{
    // What every Jolt world shares: the allocation, trace and assert hooks, the factory and registered types, and Jolt's jobs run on Trinity's job system. The first world acquires it and the last releases it, so Physics is 0 B whenever no world exists. On the main thread
    namespace JoltContext
    {
        void Acquire();
        void Release();

        [[nodiscard]] JPH::JobSystem& GetJobSystem();
    }
}