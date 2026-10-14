#pragma once

#include <filesystem>
#include <string_view>

namespace Trinity
{
    // .NET in the engine: the installed runtime found through nethost, started from Trinity-ScriptCore's runtime config, and ScriptCore loaded into the default load context, whose entry points are called through function pointers. A runtime that cannot be found or started leaves C# off with one error, and the engine runs on. Main thread only
    namespace DotNetHost
    {
        // From the Scripting folder beside the executables, which holds Trinity-ScriptCore.dll and its runtime config. Hands ScriptCore the functions of NativeFunctions.def, checks that both sides lay out the shared types alike, and logs the runtime's version and how long it took to start, once a call into C# that calls back into the engine has come back right
        bool Start(const std::filesystem::path& scriptingDirectory);
        // ScriptCore lets go of the engine's functions. The runtime itself stays, since .NET cannot leave a process it has started in
        void Stop();

        [[nodiscard]] bool IsRunning();
        // Such as .NET 10.0.0, or empty while not running
        [[nodiscard]] std::string_view GetRuntimeDescription();
    }
}