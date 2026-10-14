#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <filesystem>

namespace Trinity
{
    // The managed heap, which the garbage collector owns rather than any memory tag: its size as of the last collection, and how many collections there have been
    struct HeapStatistics
    {
        std::uint64_t Bytes = 0;
        std::uint32_t Collections = 0;
    };

    // A game's C# assembly, whose script classes register as C# classes once it loads, so Add Script lists them and a running scene makes their instances. One is loaded at a time, into a collectible load context, which unloads for a reload. Main thread only
    namespace ManagedScripts
    {
        // Whether .NET is running, so an assembly can load
        [[nodiscard]] TRINITY_API bool IsAvailable();

        // A copy of a build, which stays open while it is loaded, so the build itself can be written over. What it depends on is found beside the build it was copied from, or beside the copy without one. False, logged, when C# is off, an assembly is loaded already, or this one could not be loaded. A class that could not be registered is logged and left out, and the rest load
        TRINITY_API bool LoadAssembly(const std::filesystem::path& path, const std::filesystem::path& buildPath = {});
        // Its classes are unregistered, every instance ScriptCore still holds is freed, and its load context unloaded and collected. Every instance should have gone first, through ScriptRuntime::ReleaseInstances or the scene stopping. False when the context was not collected, which is logged with what still holds it, and its memory then stays until the process ends
        TRINITY_API bool UnloadAssembly();

        [[nodiscard]] TRINITY_API bool IsAssemblyLoaded();
        [[nodiscard]] TRINITY_API HeapStatistics GetHeapStatistics();
    }
}