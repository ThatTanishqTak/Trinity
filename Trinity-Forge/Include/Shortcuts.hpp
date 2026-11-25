#pragma once

#include <imgui.h>

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Forge's commands that a key runs, in one list. The key handling, the key shown beside a menu's item and the Keyboard Shortcuts window all read it, so a command's key is told in one place
class Shortcuts
{
public:
    struct Command
    {
        // What a saved key names the command by, which stays as its label and its key change
        std::string_view ID;
        std::string_view Category;
        std::string_view Label;
        // The key it runs on, and a second that runs it too, as Ctrl+Shift+Z redoes beside Ctrl+Y. ImGuiKey_None for none
        ImGuiKeyChord Chord = ImGuiKey_None;
        ImGuiKeyChord Alternate = ImGuiKey_None;
        // Whether the key held down runs it again and again, as Undo is
        bool Repeats = false;
        // Asked as its key is pressed. A command without it is always ready
        std::function<bool()> IsEnabled;
        std::function<void()> Run;
        // The key it was added with, which Add fills in
        ImGuiKeyChord Default = ImGuiKey_None;
    };

    // A key a panel reads for itself while it has the focus, listed beside the commands and not theirs to run
    struct PanelKey
    {
        std::string_view Panel;
        std::string_view Label;
        std::string_view Keys;
    };

    void Add(Command command);
    [[nodiscard]] std::span<const Command> GetCommands() const { return m_Commands; }
    [[nodiscard]] static std::span<const PanelKey> GetPanelKeys();
    [[nodiscard]] const Command* Find(std::string_view id) const;

    // The command's key as a menu shows it beside the item. Empty for a command with no key, and for an ID that names none
    [[nodiscard]] std::string GetKeyText(std::string_view id) const;
    // Ctrl, Alt, Shift and Super in that order, then the key, by its sign where it has one. Empty for none
    [[nodiscard]] static std::string FormatChord(ImGuiKeyChord chord);

    // Runs the first command in the list whose key was pressed this frame and that is ready. True if one ran
    bool Dispatch() const;

private:
    std::vector<Command> m_Commands;
};