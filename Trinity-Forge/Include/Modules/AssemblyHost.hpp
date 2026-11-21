#pragma once

#include "Modules/AssemblyBuild.hpp"
#include "Modules/BuildOutput.hpp"

#include <Trinity.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Scripts is watched, built on a job and reloaded. Each loaded build is a numbered copy in Cache
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

    // Writes the .csproj, Directory.Build.props, a source and the .slnx, each if missing, then builds
    [[nodiscard]] bool Create(Trinity::Project& project);
    // Built at the next Update, or once the build running ends
    void RequestBuild();

    // The first is called before the scripts are unregistered, the second once the new ones registered
    void SetReloadCallbacks(std::function<void()> beforeUnload, std::function<void()> afterLoad)
    {
        m_BeforeUnload = std::move(beforeUnload);
        m_AfterLoad = std::move(afterLoad);
    }

    // Called as each build starts, before it logs anything, as the Console's Clear on Build needs
    void SetBuildStarting(std::function<void()> buildStarting) { m_BuildStarting = std::move(buildStarting); }

    // Whether .NET is running, so C# scripts can build and load
    [[nodiscard]] static bool IsSupported() { return Trinity::ManagedScripts::IsAvailable(); }
    [[nodiscard]] bool IsBuilding() const { return m_Build.IsRunning(); }
    // The build running or waiting to, or else the last that ran to its end, not a cancelled one
    [[nodiscard]] BuildStatus GetStatus() const;
    // The lines the last build that ran to its end logged at error
    [[nodiscard]] const std::vector<std::string>& GetErrorLines() const { return m_LastErrorLines; }
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
    std::function<void()> m_BuildStarting;
    // When a source or the output last changed, which waits to settle
    std::optional<Clock::time_point> m_SourcesChangedAt;
    std::optional<Clock::time_point> m_OutputChangedAt;
    bool m_BuildRequested = false;
    BuildStatus m_LastBuild;
    std::vector<std::string> m_LastErrorLines;
    std::uint64_t m_RunningFirstLine = 0;
};