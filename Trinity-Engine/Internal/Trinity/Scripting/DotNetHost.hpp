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
    // A runtime that cannot be found or started leaves C# off with one error. Main thread only
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

        // From the Scripting folder beside the executables. Checks both sides lay out shared types alike
        bool Start(const std::filesystem::path& scriptingDirectory);
        // In place of Start: a game's NativeAOT library, which needs no .NET installed. It stays loaded
        bool StartCompiled(const std::filesystem::path& library);
        // The runtime itself stays, since .NET cannot leave a process it has started in
        void Stop();

        [[nodiscard]] bool IsRunning();
        // Such as .NET 10.0.0, or empty while not running
        [[nodiscard]] std::string_view GetRuntimeDescription();

        // Into a collectible load context. The number of classes described, or -1, which ScriptCore logged
        [[nodiscard]] std::int32_t LoadAssembly(const std::filesystem::path& path, const std::filesystem::path& buildPath);
        // The classes compiled into a NativeAOT library. The number described, or -1, as ScriptCore logged
        [[nodiscard]] std::int32_t LoadCompiled(std::string_view name);
        // False when the context was not collected, which ScriptCore has logged with what still holds it
        [[nodiscard]] bool UnloadAssembly();

        [[nodiscard]] HeapStatistics GetHeapStatistics();
        // Whether a debugger of managed code is attached, as .NET sees it
        [[nodiscard]] bool IsDebuggerAttached();

        // Held by a GC handle, or 0 when its constructor threw, which ScriptCore has logged
        [[nodiscard]] std::intptr_t CreateInstance(std::int32_t classIndex, UUID entity);
        void DestroyInstance(std::intptr_t instance);
        // False when the script threw. A string given is valid until the next call into ScriptCore
        [[nodiscard]] bool SetField(std::intptr_t instance, std::int32_t field, const Interop::FieldValue& value);
        [[nodiscard]] bool GetField(std::intptr_t instance, std::int32_t field, Interop::FieldValue& value);
        [[nodiscard]] bool SetElement(std::intptr_t instance, std::int32_t field, std::int32_t element, const Interop::FieldValue& value);
        [[nodiscard]] bool GetElement(std::intptr_t instance, std::int32_t field, std::int32_t element, Interop::FieldValue& value);
        [[nodiscard]] bool Invoke(std::intptr_t instance, ManagedCallback callback, float timestep);
        [[nodiscard]] bool InvokePhysics(std::intptr_t instance, PhysicsEventType type, const Interop::Collision& collision);

        // Held by a GC handle. Each is false when it threw, which ScriptCore has logged
        [[nodiscard]] bool ResumeTask(std::intptr_t task, std::int32_t& waitType, float& seconds, std::int32_t& frames);
        [[nodiscard]] bool CheckTask(std::intptr_t task, bool& met);
        void FreeTask(std::intptr_t task);

        // For the players whose press went down or up this frame. A handler that throws is removed
        void RaiseInputEvents();
        // False when it threw, which ScriptCore has logged. Freed as a task is, through FreeTask
        [[nodiscard]] bool InvokeUIHandler(std::intptr_t handler, std::int32_t type, std::uint64_t element, std::uint64_t target, std::string_view value);
    }
}