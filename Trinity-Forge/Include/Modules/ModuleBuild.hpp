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

// One build of a project's C++ module, on a job, against an engine package, Forge's own for the module it loads or the Distribution package for a game's player, with the compiler the engine was built with and the CMake on the PATH. The module is configured when it has not been, or was for another Forge, and built with Ninja. Its warnings and errors go to the log with their files and lines as they come, the rest of the output at trace, and a summary at the end
class ModuleBuild
{
public:
    struct Request
    {
        std::string Name;
        std::filesystem::path ProjectDirectory;
        std::filesystem::path SourceDirectory;
        // This configuration's build tree
        std::filesystem::path BuildDirectory;
        // Debug, Release or Distribution, as the engine package beside EngineDirectory was built, and what the build makes there, a module Forge loads or a game's player
        std::string Configuration;
        std::filesystem::path EngineDirectory;
        std::filesystem::path Output;
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
        // The request's Output, there once it succeeded
        std::filesystem::path Output;
    };

    ModuleBuild() = default;
    ~ModuleBuild();

    ModuleBuild(const ModuleBuild&) = delete;
    ModuleBuild& operator=(const ModuleBuild&) = delete;

    void Start(Request request);
    // On the main thread, once a frame. The result comes back once the build is over
    [[nodiscard]] std::optional<Result> Update();
    // Ends the build and everything it started, and waits for it
    void Cancel();

    [[nodiscard]] bool IsRunning() const { return m_Running != nullptr; }

    // Debug or Release. Distribution Forge has a static engine, which cannot load a module, so it builds none
    [[nodiscard]] static std::string_view GetConfiguration();
    [[nodiscard]] static bool IsSupported();
    // Forge's own package, beside its engine
    [[nodiscard]] static std::filesystem::path GetEngineDirectory();
    [[nodiscard]] static std::filesystem::path GetBuildDirectory(const std::filesystem::path& projectBuildDirectory);
    [[nodiscard]] static std::filesystem::path GetLibraryPath(const std::filesystem::path& buildDirectory, std::string_view name);

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