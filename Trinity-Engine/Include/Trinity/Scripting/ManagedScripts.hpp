#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace Trinity
{
    // The garbage collector's heap, under no memory tag: its size at the last collection, and the count
    struct HeapStatistics
    {
        std::uint64_t Bytes = 0;
        std::uint32_t Collections = 0;
    };

    // One game assembly at a time, in a collectible load context or compiled into a player. Main thread
    namespace ManagedScripts
    {
        // Whether .NET is running, so an assembly can load
        [[nodiscard]] TRINITY_API bool IsAvailable();

        // Loads a copy, so the build can be written over. False, logged, if C# is off or one is loaded
        TRINITY_API bool LoadAssembly(const std::filesystem::path& path, const std::filesystem::path& buildPath = {});
        // A player's NativeAOT library, started in place of .NET. False, logged, if C# is running already
        TRINITY_API bool StartCompiled(const std::filesystem::path& library, std::string_view assembly);
        // Every instance should have gone first. False, logged, if the context was not collected
        TRINITY_API bool UnloadAssembly();

        [[nodiscard]] TRINITY_API bool IsAssemblyLoaded();
        [[nodiscard]] TRINITY_API HeapStatistics GetHeapStatistics();
        // A debugger of managed code, which Process::IsDebuggerAttached does not see. False without .NET
        [[nodiscard]] TRINITY_API bool IsDebuggerAttached();
    }
}