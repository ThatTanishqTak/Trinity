#pragma once

#include "Trinity/Core/Export.hpp"

#include <filesystem>

namespace Trinity
{
    // A game's C# assembly, whose script classes register as C# classes once it loads, so Add Script lists them and a running scene makes their instances. One is loaded at a time, into a collectible load context, from its bytes, so a build can write over its file while it runs. Main thread only
    namespace ManagedScripts
    {
        // Whether .NET is running, so an assembly can load
        [[nodiscard]] TRINITY_API bool IsAvailable();

        // False, logged, when C# is off, an assembly is loaded already, or this one could not be loaded. A class that could not be registered is logged and left out, and the rest load
        TRINITY_API bool LoadAssembly(const std::filesystem::path& path);
        // Its classes are unregistered and its load context unloaded. Every instance of them must have gone first, through ScriptRuntime::ReleaseInstances or the scene stopping
        TRINITY_API void UnloadAssembly();

        [[nodiscard]] TRINITY_API bool IsAssemblyLoaded();
    }
}