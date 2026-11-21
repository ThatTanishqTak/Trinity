#pragma once

#include "Modules/BuildOutput.hpp"
#include "Modules/ModuleBuild.hpp"

#include <Trinity.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Source is watched, built on a job and reloaded. Each good build loads as a numbered copy in Cache
class ModuleHost
{
public:
    ModuleHost() = default;
    ~ModuleHost();

    ModuleHost(const ModuleHost&) = delete;
    ModuleHost& operator=(const ModuleHost&) = delete;

    // The project must outlive Close
    void Open(Trinity::Project& project);
    // Any build is cancelled and the module unloaded
    void Close();
    // On the main thread, once a frame
    void Update();

    // Writes Source/CMakeLists.txt and a source, each if missing, named after the project, then builds
    [[nodiscard]] bool Create(Trinity::Project& project);
    // Built at the next Update, or once the build running ends
    void RequestBuild();

    // The first is called before the module is unregistered, the second once the new one registered
    void SetReloadCallbacks(std::function<void()> beforeUnload, std::function<void()> afterLoad)
    {
        m_BeforeUnload = std::move(beforeUnload);
        m_AfterLoad = std::move(afterLoad);
    }

    // Called as each build starts, before it logs anything, as the Console's Clear on Build needs
    void SetBuildStarting(std::function<void()> buildStarting) { m_BuildStarting = std::move(buildStarting); }

    [[nodiscard]] static bool IsSupported() { return ModuleBuild::IsSupported(); }
    [[nodiscard]] bool IsBuilding() const { return m_Build.IsRunning(); }
    // The build running or waiting to, or else the last that ran to its end, not a cancelled one
    [[nodiscard]] BuildStatus GetStatus() const;
    // The lines the last build that ran to its end logged at error
    [[nodiscard]] const std::vector<std::string>& GetErrorLines() const { return m_LastErrorLines; }
    [[nodiscard]] bool IsLoaded() const { return m_Library.IsLoaded(); }

private:
    using Clock = std::chrono::steady_clock;

    void Watch();
    void StartBuild();
    void Load(const std::filesystem::path& built);
    [[nodiscard]] std::optional<std::filesystem::path> CopyBuild(const std::filesystem::path& built);
    [[nodiscard]] bool LoadCopy(const std::filesystem::path& copy);
    void Unload();
    void KeepVariables();
    void RestoreVariables();
    void RemoveStaleCopies() const;
    [[nodiscard]] std::filesystem::path GetCopyDirectory() const;

    Trinity::Project* m_Project = nullptr;
    Trinity::DirectoryWatcher m_Watcher;
    ModuleBuild m_Build;
    Trinity::SharedLibrary m_Library;
    Trinity::GameModule::UnloadFunction m_Unload = nullptr;
    std::filesystem::path m_LoadedCopy;
    std::uint32_t m_NextCopy = 1;
    std::function<void()> m_BeforeUnload;
    std::function<void()> m_AfterLoad;
    std::function<void()> m_BuildStarting;
    // The console variables the loaded module made, and their values, which the next module gets
    std::vector<std::string> m_ModuleVariables;
    std::vector<std::pair<std::string, std::string>> m_KeptValues;
    // When a source last changed, which a build waits to settle
    std::optional<Clock::time_point> m_ChangedAt;
    bool m_BuildRequested = false;
    BuildStatus m_LastBuild;
    std::vector<std::string> m_LastErrorLines;
    std::uint64_t m_RunningFirstLine = 0;
};