#pragma once

#include "Trinity/Core/UUID.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Scripting/Interop.hpp"
#include "Trinity/Scripting/ManagedScripts.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace Trinity
{
    // .NET in the engine: the installed runtime found through nethost, started from Trinity-ScriptCore's runtime config, and ScriptCore loaded into the default load context, whose entry points are called through function pointers. A runtime that cannot be found or started leaves C# off with one error, and the engine runs on. Main thread only
    namespace DotNetHost
    {
        // A script callback, numbered as Trinity.Interop.ScriptCallback numbers it
        enum class ManagedCallback : std::int32_t
        {
            Create,
            FixedUpdate,
            Update,
            LateUpdate,
            Destroy,
            Reloaded
        };

        // From the Scripting folder beside the executables, which holds Trinity-ScriptCore.dll and its runtime config. Hands ScriptCore the functions of NativeFunctions.def, checks that both sides lay out the shared types alike, and logs the runtime's version and how long it took to start, once a call into C# that calls back into the engine has come back right
        bool Start(const std::filesystem::path& scriptingDirectory);
        // Unloads a game assembly still loaded, and ScriptCore lets go of the engine's functions. The runtime itself stays, since .NET cannot leave a process it has started in
        void Stop();

        [[nodiscard]] bool IsRunning();
        // Such as .NET 10.0.0, or empty while not running
        [[nodiscard]] std::string_view GetRuntimeDescription();

        // A copy of a game assembly, loaded into a collectible load context, whose script classes ScriptCore describes through Scripting_BeginClass and the rest of them as it loads. What it depends on is found beside the build it was copied from. The number of classes it described, or -1, which ScriptCore has logged, when it could not be loaded
        [[nodiscard]] std::int32_t LoadAssembly(const std::filesystem::path& path, const std::filesystem::path& buildPath);
        // Frees every instance still held and unloads the assembly's context. False when the context was not collected, which ScriptCore has logged with what still holds it
        [[nodiscard]] bool UnloadAssembly();

        [[nodiscard]] HeapStatistics GetHeapStatistics();
        // Whether a debugger of managed code is attached, as .NET sees it
        [[nodiscard]] bool IsDebuggerAttached();

        // An instance of the class ScriptCore described at that index, for the entity, held by a GC handle, or 0 when its constructor threw, which ScriptCore has logged
        [[nodiscard]] std::intptr_t CreateInstance(std::int32_t classIndex, UUID entity);
        void DestroyInstance(std::intptr_t instance);
        // Each is false when the script threw, which ScriptCore has logged with the C# stack trace. A string GetField gives is valid until the next call into ScriptCore
        [[nodiscard]] bool SetField(std::intptr_t instance, std::int32_t field, const Interop::FieldValue& value);
        [[nodiscard]] bool GetField(std::intptr_t instance, std::int32_t field, Interop::FieldValue& value);
        [[nodiscard]] bool Invoke(std::intptr_t instance, ManagedCallback callback, float timestep);
        [[nodiscard]] bool InvokePhysics(std::intptr_t instance, PhysicsEventType type, const Interop::Collision& collision);

        // A C# timer or coroutine, held by a GC handle: resumed until it next waits, with the wait's type as a ScriptWaitType and its seconds or frames, its condition asked, and freed. Each is false when it threw, which ScriptCore has logged
        [[nodiscard]] bool ResumeTask(std::intptr_t task, std::int32_t& waitType, float& seconds, std::int32_t& frames);
        [[nodiscard]] bool CheckTask(std::intptr_t task, bool& met);
        void FreeTask(std::intptr_t task);
    }
}