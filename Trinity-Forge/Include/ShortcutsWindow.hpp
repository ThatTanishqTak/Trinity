#pragma once

#include "EditorSession.hpp"

#include <imgui.h>

#include <string>
#include <string_view>

// Help's Keyboard Shortcuts: every command with its key, by category, then the keys each panel reads for itself, narrowed by what is typed. A click on a command's key asks for another, which is saved for each person
class ShortcutsWindow
{
public:
    explicit ShortcutsWindow(EditorSession& session);

    // Opens it, or brings it to the front where it is open already
    void Open();
    void Draw();

private:
    void DrawCommands(std::string_view category, bool& any);
    void DrawKeyPopup();

    EditorSession& m_Session;
    std::string m_Search;
    // The command a key is being asked for, by its ID, and the key pressed for it so far
    std::string m_Asking;
    ImGuiKeyChord m_Pressed = ImGuiKey_None;
    bool m_OpenKeyPopup = false;
    bool m_Open = false;
    bool m_FocusRequested = false;
};