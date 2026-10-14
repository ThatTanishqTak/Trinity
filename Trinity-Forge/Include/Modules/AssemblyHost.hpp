#pragma once

#include "Modules/AssemblyBuild.hpp"

#include <Trinity.hpp>

#include <functional>
#include <utility>

// The open project's C# scripts: built on a job once the project opens and when asked, and loaded after the first good build into a collectible load context, from the assembly's bytes, so a later build can write over the file. Scripts/Trinity.Local.props, which names this Forge's Trinity-ScriptCore for an IDE's builds, is written as the project opens
class AssemblyHost
{
public:
    AssemblyHost() = default;
    ~AssemblyHost();

    AssemblyHost(const AssemblyHost&) = delete;
    AssemblyHost& operator=(const AssemblyHost&) = delete;

    // The project must outlive Close
    void Open(Trinity::Project& project);
    // Any build is cancelled and the assembly unloaded. A running scene must have stopped first
    void Close();
    // On the main thread, once a frame
    void Update();

    // Scripts/<name>.csproj, Scripts/Directory.Build.props and a source for the scripts, from templates, and <name>.slnx beside the project file, each written only when it is not there, and the assembly named after the project. The project is saved by the caller. Then it builds
    [[nodiscard]] bool Create(Trinity::Project& project);
    // Built at the next Update, or once the build running ends
    void RequestBuild();

    // Called once the assembly has loaded and registered its classes, so whatever runs scripts can make their instances
    void SetLoadCallback(std::function<void()> afterLoad) { m_AfterLoad = std::move(afterLoad); }

    // Whether .NET is running, so C# scripts can build and load
    [[nodiscard]] static bool IsSupported() { return Trinity::ManagedScripts::IsAvailable(); }
    [[nodiscard]] bool IsBuilding() const { return m_Build.IsRunning(); }
    [[nodiscard]] bool IsLoaded() const { return m_Loaded; }

private:
    void WriteLocalProps() const;
    void StartBuild();
    void Load(const std::filesystem::path& built);

    Trinity::Project* m_Project = nullptr;
    AssemblyBuild m_Build;
    std::function<void()> m_AfterLoad;
    bool m_Loaded = false;
    bool m_BuildRequested = false;
};