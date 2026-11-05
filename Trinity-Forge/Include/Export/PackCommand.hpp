#pragma once

#include "Modules/AssemblyBuild.hpp"

#include <Trinity.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class EditorSession;

// Writes the open project as a game a player can run, as its export settings say, from the Build panel or from Forge's --pack=<folder>, which then closes Forge. Once every import and the builds of the module and the scripts have finished, the C# scripts are compiled ahead of time with NativeAOT, and BuildWalk gathers the build's ticked scenes, in their order, into Game.trpak in the folder, with a Game.trgame of the product, company, version, window and graphics APIs the settings name and the project's physics, audio and input. The module's build is copied beside it for a player with the shared engine, and the scripts' library for every player, so Trinity-Runtime --pack=<folder>/Game.trpak plays the game. Step by step it becomes the export
class PackCommand
{
public:
    explicit PackCommand(EditorSession& session) : m_Session(session)
    {

    }

    // Into the folder, closing Forge once it is written or has failed when asked to, as --pack does. Nothing while one is under way
    void Start(const std::filesystem::path& folder, bool closeWhenDone = true);
    // Once a frame. Waits while the project imports or builds, then while its scripts compile ahead of time, then writes
    void Update();
    [[nodiscard]] bool IsRunning() const { return m_Folder.has_value(); }

    // What stops the project's settings from making a game, each a sentence for the Build panel and the Console: a scene of the build that is missing or is not a scene, no scene ticked, no product or company, which its saves and log go under, no graphics API, or no output folder. Empty when nothing does
    [[nodiscard]] static std::vector<std::string> CheckSettings(const Trinity::Project& project, const Trinity::AssetRegistry& registry);

private:
    // Once, for a project with C# scripts. True while it runs
    [[nodiscard]] bool CompileScripts();
    [[nodiscard]] bool Write(const std::filesystem::path& folder);

    EditorSession& m_Session;
    std::optional<std::filesystem::path> m_Folder;
    bool m_CloseWhenDone = true;
    AssemblyBuild m_Compiler;
    std::optional<AssemblyBuild::Result> m_Compiled;
};