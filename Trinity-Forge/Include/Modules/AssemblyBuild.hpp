#pragma once

#include <Trinity.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// One build of a project's C# scripts, on a job, with dotnet build in Debug for a Debug Forge and Release otherwise, against the Trinity-ScriptCore and Trinity-ScriptGenerator beside Forge. The .NET SDK is the one on the PATH, or where DOTNET_ROOT points. Its warnings and errors go to the log with their files and lines as they come, the rest of the output at trace, and a summary at the end
class AssemblyBuild
{
public:
    struct Request
    {
        std::string Name;
        std::filesystem::path ProjectDirectory;
        std::filesystem::path ScriptsDirectory;
    };

    struct Result
    {
        bool Succeeded = false;
        bool Cancelled = false;
        std::uint32_t Errors = 0;
        std::uint32_t Warnings = 0;
        double Seconds = 0.0;
        // Each line it logged at error, as BuildOutput keeps them
        std::vector<std::string> ErrorLines;
        std::filesystem::path Assembly;
    };

    AssemblyBuild() = default;
    ~AssemblyBuild();

    AssemblyBuild(const AssemblyBuild&) = delete;
    AssemblyBuild& operator=(const AssemblyBuild&) = delete;

    void Start(Request request);
    // On the main thread, once a frame. The result comes back once the build is over
    [[nodiscard]] std::optional<Result> Update();
    // Ends the build and everything it started, and waits for it
    void Cancel();

    [[nodiscard]] bool IsRunning() const { return m_Running != nullptr; }

    [[nodiscard]] static std::string_view GetConfiguration();
    // The Scripting folder beside Forge, which holds Trinity-ScriptCore and the source generator
    [[nodiscard]] static std::filesystem::path GetScriptingDirectory();
    // Where the C# project's Directory.Build.props sends every build of it, Forge's and an IDE's alike: Build/Scripts/bin/<name>/<configuration>/<name>.dll
    [[nodiscard]] static std::filesystem::path GetAssemblyPath(const std::filesystem::path& projectDirectory, std::string_view name);

private:
    // The flags are read by both threads, and the result written by the job before Done
    struct Running
    {
        Request Work;
        Result Outcome;
        std::atomic<bool> Cancel = false;
        std::atomic<bool> Done = false;
    };

    static void Run(Running& running);

    std::unique_ptr<Running> m_Running;
    Trinity::JobCounter m_Jobs;
};