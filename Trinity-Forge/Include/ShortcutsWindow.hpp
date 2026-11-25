#pragma once

#include "EditorSession.hpp"

#include <string>

// Help's Keyboard Shortcuts: every command with its key, by category, then the keys each panel reads for itself, narrowed by what is typed
class ShortcutsWindow
{
public:
    explicit ShortcutsWindow(EditorSession& session);

    // Opens it, or brings it to the front where it is open already
    void Open();
    void Draw();

private:
    EditorSession& m_Session;
    std::string m_Search;
    bool m_Open = false;
    bool m_FocusRequested = false;
};