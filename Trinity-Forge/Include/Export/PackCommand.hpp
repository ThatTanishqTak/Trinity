#pragma once

#include "Modules/AssemblyBuild.hpp"

#include <Trinity.hpp>

#include <filesystem>
#include <optional>

class EditorSession;

// Forge's --pack=<folder>, which writes the open project as a game a player can run, then closes Forge. Once every import and the builds of the module and the scripts have finished, the C# scripts are compiled ahead of time with NativeAOT, and BuildWalk gathers every scene in the project, the start scene first, into Game.trpak in the folder, with a Game.trgame of the project's settings. The module's build is copied beside it for a player with the shared engine, and the scripts' library for every player, so Trinity-Runtime --pack=<folder>/Game.trpak plays the game. Step by step it becomes the export
class PackCommand
{
public:
    explicit PackCommand(EditorSession& session) : m_Session(session)
    {

    }

    void Start(const std::filesystem::path& folder);
    // Once a frame. Waits while the project imports or builds, then while its scripts compile ahead of time, then writes and closes Forge
    void Update();

private:
    // Once, for a project with C# scripts. True while it runs
    [[nodiscard]] bool CompileScripts();
    [[nodiscard]] bool Write(const std::filesystem::path& folder);

    EditorSession& m_Session;
    std::optional<std::filesystem::path> m_Folder;
    AssemblyBuild m_Compiler;
    std::optional<AssemblyBuild::Result> m_Compiled;
};