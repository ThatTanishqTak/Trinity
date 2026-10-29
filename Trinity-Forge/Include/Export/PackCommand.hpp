#pragma once

#include <Trinity.hpp>

#include <filesystem>
#include <optional>

class EditorSession;

// Forge's --pack=<folder>, which writes the open project as a game a player can run, then closes Forge. Once every import and the module's build have finished, BuildWalk gathers every scene in the project, the start scene first, into Game.trpak in the folder, with a Game.trgame of the project's settings, and the module's build is copied beside it for a player with the shared engine, so Trinity-Runtime --pack=<folder>/Game.trpak plays the game. Step by step it becomes the export
class PackCommand
{
public:
    explicit PackCommand(EditorSession& session) : m_Session(session)
    {

    }

    void Start(const std::filesystem::path& folder);
    // Once a frame. Waits while the project imports or builds, then writes and closes Forge
    void Update();

private:
    [[nodiscard]] bool Write(const std::filesystem::path& folder);

    EditorSession& m_Session;
    std::optional<std::filesystem::path> m_Folder;
};