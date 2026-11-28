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
    // Each person's changed keys, a command a line, kept with the preferences
    static constexpr std::string_view c_Path = "/saves/Shortcuts.txt";

    struct Command
    {
        // What a saved key names the command by, which stays as its label and its key change
        std::string ID;
        std::string_view Category;
        std::string Label;
        // The key it runs on, and a second that runs it too, as Ctrl+Shift+Z redoes beside Ctrl+Y. ImGuiKey_None for none
        ImGuiKeyChord Chord = ImGuiKey_None;
        ImGuiKeyChord Alternate = ImGuiKey_None;
        // Whether the key held down runs it again and again, as Undo is
        bool Repeats = false;
        // Asked as its key is pressed. A command without it is always ready
        std::function<bool()> IsEnabled{};
        // Asked of its key alone, and not by the Command Palette: where the key acts, as over one panel. A command without it acts everywhere
        std::function<bool()> IsKeyInScope{};
        // The panel its key acts in, as its tab names it, or empty for everywhere. Keys clash only among commands of one panel, or among those of none, and a panel's key goes before an everywhere one's while that panel has it
        std::string_view Panel{};
        std::function<void()> Run{};
        // The keys it was added with, which Add fills in and a reset puts back
        ImGuiKeyChord Default = ImGuiKey_None;
        ImGuiKeyChord DefaultAlternate = ImGuiKey_None;

        [[nodiscard]] bool IsDefault() const { return Chord == Default && Alternate == DefaultAlternate; }
    };

    // What a panel does with the mouse, listed beside the commands and not theirs to run
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
    // The text with the command's key after it in brackets, as a tooltip names it, or the text alone while the command has no key
    [[nodiscard]] std::string WithKey(std::string_view text, std::string_view id) const;
    // Ctrl, Alt, Shift and Super in that order, then the key, by its sign where it has one. Empty for none
    [[nodiscard]] static std::string FormatChord(ImGuiKeyChord chord);

    // The reverse of FormatChord, for a saved key. ImGuiKey_None for text that names no key
    [[nodiscard]] static ImGuiKeyChord ParseChord(std::string_view text);

    // Another command of the same panel, or of none, that runs on the key, by either of its two, or null
    [[nodiscard]] const Command* FindUser(ImGuiKeyChord chord, std::string_view except) const;
    // A command the key is shared with, of a panel where the other acts everywhere or the other way round, which runs first in its panel, or null
    [[nodiscard]] const Command* FindSharer(ImGuiKeyChord chord, std::string_view except) const;

    // The command's key from now on, taken from any other command that ran on it. ImGuiKey_None leaves the command with none. Saved at once
    void SetKey(std::string_view id, ImGuiKeyChord chord);
    void ResetKey(std::string_view id);
    void ResetAll();
    // Once every command is added, so each saved key finds its command
    void ReadKeys();

    // Runs the first command whose key was pressed this frame where it acts, and that is ready, a panel's before an everywhere one's. True if one ran
    bool Dispatch() const;

private:
    [[nodiscard]] Command* FindCommand(std::string_view id);
    void WriteKeys() const;

    std::vector<Command> m_Commands;
};