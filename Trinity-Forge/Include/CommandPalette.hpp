#pragma once

#include "EditorSession.hpp"

#include <cstddef>
#include <string>
#include <vector>

// Every command of the shortcut registry, found by typing some of its name and run with Enter, as an editor's Ctrl+K or Ctrl+Shift+P
class CommandPalette
{
public:
    explicit CommandPalette(EditorSession& session);

    void Open() { m_OpenRequested = true; }
    void Draw();

private:
    [[nodiscard]] std::vector<const Shortcuts::Command*> FindMatches() const;

    EditorSession& m_Session;
    std::string m_Search;
    // The row Enter runs, among the commands found
    std::size_t m_Chosen = 0;
    // The commands run from here, the last first, which an empty search lists before the rest
    std::vector<std::string> m_Recent;
    bool m_OpenRequested = false;
    // The arrows moved the choice, so its row is scrolled to
    bool m_ScrollToChosen = false;
};