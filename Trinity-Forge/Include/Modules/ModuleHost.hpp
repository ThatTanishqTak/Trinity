#pragma once

#include "Modules/ModuleBuild.hpp"

#include <Trinity.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <utility>

// The open project's C++ game module: its Source folder watched, built on a job once the project opens and once its files stop changing, and loaded after each good build. Each build unloads the module first, since the library it writes is the one loaded, which Windows keeps from being written
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

    // Source/CMakeLists.txt and a source for the module, from templates, each written only when it is not there, and the module named after the project. The project is saved by the caller. Then it builds
    [[nodiscard]] bool Create(Trinity::Project& project);
    // Built at the next Update, or once the build running ends
    void RequestBuild();

    // Called while the module is still loaded, before anything of it is unregistered, so whatever runs its code can stop first
    void SetBeforeUnload(std::function<void()> callback) { m_BeforeUnload = std::move(callback); }

    [[nodiscard]] static bool IsSupported() { return ModuleBuild::IsSupported(); }
    [[nodiscard]] bool IsBuilding() const { return m_Build.IsRunning(); }
    [[nodiscard]] bool IsLoaded() const { return m_Library.IsLoaded(); }

private:
    using Clock = std::chrono::steady_clock;

    void Watch();
    void StartBuild();
    void Load(const std::filesystem::path& library);
    void Unload();

    Trinity::Project* m_Project = nullptr;
    Trinity::DirectoryWatcher m_Watcher;
    ModuleBuild m_Build;
    Trinity::SharedLibrary m_Library;
    Trinity::GameModule::UnloadFunction m_Unload = nullptr;
    std::function<void()> m_BeforeUnload;
    // When a source last changed, which a build waits to settle
    std::optional<Clock::time_point> m_ChangedAt;
    bool m_BuildRequested = false;
};