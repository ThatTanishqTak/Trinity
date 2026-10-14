#pragma once

#include "Modules/AssemblyBuild.hpp"

#include <Trinity.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <utility>

// The open project's C# scripts: Scripts is watched, built on a job once the project opens and once its files stop changing, and reloaded after each good build. The build's output is watched as well, so a build from an IDE reloads too, and the build Forge loaded already is not loaded again. Each build that loads is copied to Cache/Scripts under a number of its own, with its PDB under the same number, so the build can write over its files while a copy runs, and a debugger finds the symbols of the copy that runs. A failed build leaves the scripts that run as they are. Scripts/Trinity.Local.props, which names this Forge's Trinity-ScriptCore for an IDE's builds, is written as the project opens
class AssemblyHost
{
public:
    AssemblyHost() = default;
    ~AssemblyHost();

    AssemblyHost(const AssemblyHost&) = delete;
    AssemblyHost& operator=(const AssemblyHost&) = delete;

    // The project must outlive Close
    void Open(Trinity::Project& project);
    // Any build is cancelled and the scripts unloaded. A running scene must have stopped first
    void Close();
    // On the main thread, once a frame
    void Update();

    // Scripts/<name>.csproj, Scripts/Directory.Build.props and a source for the scripts, from templates, and <name>.slnx beside the project file, each written only when it is not there, and the assembly named after the project. The project is saved by the caller. Then it builds
    [[nodiscard]] bool Create(Trinity::Project& project);
    // Built at the next Update, or once the build running ends
    void RequestBuild();

    // The first is called while the scripts are still loaded, before anything of them is unregistered, and the second once scripts loaded in their place have registered, so whatever runs them can let go of the old instances and make new ones
    void SetReloadCallbacks(std::function<void()> beforeUnload, std::function<void()> afterLoad)
    {
        m_BeforeUnload = std::move(beforeUnload);
        m_AfterLoad = std::move(afterLoad);
    }

    // Whether .NET is running, so C# scripts can build and load
    [[nodiscard]] static bool IsSupported() { return Trinity::ManagedScripts::IsAvailable(); }
    [[nodiscard]] bool IsBuilding() const { return m_Build.IsRunning(); }
    [[nodiscard]] bool IsLoaded() const { return Trinity::ManagedScripts::IsAssemblyLoaded(); }

private:
    using Clock = std::chrono::steady_clock;

    void Watch();
    void WriteLocalProps() const;
    void StartBuild();
    void Load(const std::filesystem::path& built);
    [[nodiscard]] std::optional<std::filesystem::path> CopyBuild(const std::filesystem::path& built);
    [[nodiscard]] bool LoadCopy(const std::filesystem::path& copy, const std::filesystem::path& built);
    // True when the scripts' context was collected, so their copy can be removed
    bool Unload();
    void RemoveStaleCopies() const;
    [[nodiscard]] std::filesystem::path GetCopyDirectory() const;
    [[nodiscard]] std::filesystem::path GetBuiltPath() const;

    Trinity::Project* m_Project = nullptr;
    AssemblyBuild m_Build;
    Trinity::DirectoryWatcher m_SourceWatcher;
    Trinity::DirectoryWatcher m_OutputWatcher;
    std::filesystem::path m_LoadedCopy;
    // When the build last loaded, or that failed to, was written, so the same build is not tried twice
    std::optional<std::filesystem::file_time_type> m_TriedBuildTime;
    std::uint32_t m_NextCopy = 1;
    std::function<void()> m_BeforeUnload;
    std::function<void()> m_AfterLoad;
    // When a source or the output last changed, which waits to settle
    std::optional<Clock::time_point> m_SourcesChangedAt;
    std::optional<Clock::time_point> m_OutputChangedAt;
    bool m_BuildRequested = false;
};