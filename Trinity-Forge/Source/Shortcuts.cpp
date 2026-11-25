#include "Shortcuts.hpp"

#include <Trinity.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <utility>

namespace
{
    // The keys ImGui names by a word and a keyboard marks with a sign, and the arrows by the shorter names a menu gives them
    struct KeySign
    {
        ImGuiKey Key = ImGuiKey_None;
        std::string_view Sign;
    };

    constexpr std::array<KeySign, 15> c_KeySigns{ {
        { ImGuiKey_UpArrow, "Up" },
        { ImGuiKey_DownArrow, "Down" },
        { ImGuiKey_LeftArrow, "Left" },
        { ImGuiKey_RightArrow, "Right" },
        { ImGuiKey_Comma, "," },
        { ImGuiKey_Period, "." },
        { ImGuiKey_Slash, "/" },
        { ImGuiKey_Backslash, "\\" },
        { ImGuiKey_Semicolon, ";" },
        { ImGuiKey_Apostrophe, "'" },
        { ImGuiKey_Minus, "-" },
        { ImGuiKey_Equal, "=" },
        { ImGuiKey_LeftBracket, "[" },
        { ImGuiKey_RightBracket, "]" },
        { ImGuiKey_GraveAccent, "`" }
    } };

    constexpr std::array<std::pair<ImGuiKeyChord, std::string_view>, 4> c_Mods{ { { ImGuiMod_Ctrl, "Ctrl" },{ ImGuiMod_Alt, "Alt" },{ ImGuiMod_Shift, "Shift" },{ ImGuiMod_Super, "Super" } } };
    // What a command with no key is saved as, so a key taken away stays away
    constexpr std::string_view c_NoKey = "None";
    // After a command's ID in the file, for its second key
    constexpr std::string_view c_AlternateSuffix = ".alternate";

    // Read by each panel in its own ReadShortcuts, so these are told here by hand and are not yet a command's to change
    constexpr std::array<Shortcuts::PanelKey, 26> c_PanelKeys{ {
        { "Scene", "Move", "W" },
        { "Scene", "Rotate", "E" },
        { "Scene", "Scale", "R" },
        { "Scene", "Local or World Axes", "X" },
        { "Scene", "Toggle Snapping", "Ctrl+\\" },
        { "Scene", "Frame Selection", "F" },
        { "Scene", "Frame and Follow Selection", "Shift+F" },
        { "Hierarchy", "Rename", "F2" },
        { "Hierarchy", "Hide or Show in Scene", "H" },
        { "Hierarchy", "Frame Selection", "F" },
        { "Hierarchy", "Frame and Follow Selection", "Shift+F" },
        { "Hierarchy", "Search", "Ctrl+F" },
        { "Hierarchy", "Move Up or Down Among Siblings", "Ctrl+Up, Ctrl+Down" },
        { "Content Browser", "Rename", "F2" },
        { "Content Browser", "Delete", "Delete" },
        { "Content Browser", "Duplicate", "Ctrl+D" },
        { "Content Browser", "Copy Path", "Ctrl+Alt+C" },
        { "Content Browser", "Search", "Ctrl+F" },
        { "Content Browser", "Back", "Alt+Left" },
        { "Content Browser", "Forward", "Alt+Right" },
        { "Content Browser", "Thumbnail Size", "Ctrl+Wheel" },
        { "Audio Mixer, Sound Event, Input Actions", "Rename", "F2" },
        { "Audio Mixer, Sound Event, Input Actions", "Delete", "Delete" },
        { "Audio Mixer, Sound Event, Input Actions", "Duplicate", "Ctrl+D" },
        { "Properties", "Drag a Number", "Drag its label" },
        { "Properties", "Type a Number", "Ctrl+Click its field" }
    } };
}

// A command's ID is its own, as a saved key is found by it
void Shortcuts::Add(Command command)
{
    TR_ASSERT(Find(command.ID) == nullptr, "Shortcuts: {} is added twice", command.ID);
    command.Default = command.Chord;
    command.DefaultAlternate = command.Alternate;
    m_Commands.push_back(std::move(command));
}

std::span<const Shortcuts::PanelKey> Shortcuts::GetPanelKeys()
{
    return c_PanelKeys;
}

const Shortcuts::Command* Shortcuts::Find(std::string_view id) const
{
    const auto a_Found = std::ranges::find(m_Commands, id, &Command::ID);

    return a_Found != m_Commands.end() ? &*a_Found : nullptr;
}

std::string Shortcuts::GetKeyText(std::string_view id) const
{
    const Command* l_Command = Find(id);

    return l_Command != nullptr ? FormatChord(l_Command->Chord) : std::string();
}

std::string Shortcuts::FormatChord(ImGuiKeyChord chord)
{
    const ImGuiKey l_Key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    if (l_Key == ImGuiKey_None)
    {
        return {};
    }

    std::string l_Text;
    for (const auto& [it_Mod, it_Name] : c_Mods)
    {
        if ((chord & it_Mod) != 0)
        {
            l_Text += std::format("{}+", it_Name);
        }
    }

    const auto a_Sign = std::ranges::find(c_KeySigns, l_Key, &KeySign::Key);
    l_Text += a_Sign != c_KeySigns.end() ? a_Sign->Sign : std::string_view(ImGui::GetKeyName(l_Key));

    return l_Text;
}

// A modifier's name before each plus, then the key by its sign or by ImGui's name for it
ImGuiKeyChord Shortcuts::ParseChord(std::string_view text)
{
    ImGuiKeyChord l_Chord = ImGuiKey_None;
    std::size_t l_Plus = text.find('+');
    while (l_Plus != std::string_view::npos)
    {
        const auto a_Mod = std::ranges::find(c_Mods, text.substr(0, l_Plus), &std::pair<ImGuiKeyChord, std::string_view>::second);
        if (a_Mod == c_Mods.end())
        {
            return ImGuiKey_None;
        }

        l_Chord |= a_Mod->first;
        text.remove_prefix(l_Plus + 1);
        l_Plus = text.find('+');
    }

    if (const auto a_Sign = std::ranges::find(c_KeySigns, text, &KeySign::Sign); a_Sign != c_KeySigns.end())
    {
        return l_Chord | a_Sign->Key;
    }

    for (int it_Key = ImGuiKey_NamedKey_BEGIN; it_Key < ImGuiKey_GamepadStart; ++it_Key)
    {
        if (text == ImGui::GetKeyName(static_cast<ImGuiKey>(it_Key)))
        {
            return l_Chord | it_Key;
        }
    }

    return ImGuiKey_None;
}

const Shortcuts::Command* Shortcuts::FindUser(ImGuiKeyChord chord, std::string_view except) const
{
    const auto a_Found = std::ranges::find_if(m_Commands, [chord, except](const Command& command) { return command.ID != except && chord != ImGuiKey_None && (command.Chord == chord || command.Alternate == chord); });

    return a_Found != m_Commands.end() ? &*a_Found : nullptr;
}

// By how the key is written, each of a panel's keys being told as text, some of them two with a comma between
const Shortcuts::PanelKey* Shortcuts::FindPanelUser(ImGuiKeyChord chord)
{
    const std::string l_Text = FormatChord(chord);
    const auto a_Found = std::ranges::find_if(c_PanelKeys, [&l_Text](const PanelKey& key)
    {
        std::string_view l_Keys = key.Keys;
        while (!l_Text.empty() && !l_Keys.empty())
        {
            const std::size_t l_Comma = l_Keys.find(", ");
            if (l_Keys.substr(0, l_Comma) == l_Text)
            {
                return true;
            }

            l_Keys = l_Comma != std::string_view::npos ? l_Keys.substr(l_Comma + 2) : std::string_view();
        }

        return false;
    });

    return a_Found != c_PanelKeys.end() ? &*a_Found : nullptr;
}

Shortcuts::Command* Shortcuts::FindCommand(std::string_view id)
{
    const auto a_Found = std::ranges::find(m_Commands, id, &Command::ID);

    return a_Found != m_Commands.end() ? &*a_Found : nullptr;
}

void Shortcuts::SetKey(std::string_view id, ImGuiKeyChord chord)
{
    Command* l_Command = FindCommand(id);
    if (l_Command == nullptr)
    {
        return;
    }

    // A key runs one command, so the others let go of it, whichever of their two it was
    for (Command& it_Other : m_Commands)
    {
        if (chord != ImGuiKey_None && it_Other.Chord == chord)
        {
            it_Other.Chord = ImGuiKey_None;
        }

        if (chord != ImGuiKey_None && it_Other.Alternate == chord)
        {
            it_Other.Alternate = ImGuiKey_None;
        }
    }

    l_Command->Chord = chord;
    WriteKeys();
}

// Both of its keys as it was added with, taken from any other command given either since
void Shortcuts::ResetKey(std::string_view id)
{
    Command* l_Command = FindCommand(id);
    if (l_Command == nullptr)
    {
        return;
    }

    const ImGuiKeyChord l_Default = l_Command->Default;
    const ImGuiKeyChord l_Alternate = l_Command->DefaultAlternate;
    const auto a_IsEither = [l_Default, l_Alternate](ImGuiKeyChord chord) { return chord != ImGuiKey_None && (chord == l_Default || chord == l_Alternate); };
    for (Command& it_Other : m_Commands)
    {
        if (a_IsEither(it_Other.Chord))
        {
            it_Other.Chord = ImGuiKey_None;
        }

        if (a_IsEither(it_Other.Alternate))
        {
            it_Other.Alternate = ImGuiKey_None;
        }
    }

    l_Command->Chord = l_Default;
    l_Command->Alternate = l_Alternate;
    WriteKeys();
}

void Shortcuts::ResetAll()
{
    for (Command& it_Command : m_Commands)
    {
        it_Command.Chord = it_Command.Default;
        it_Command.Alternate = it_Command.DefaultAlternate;
    }

    WriteKeys();
}

// A line a command: its ID, an equals sign and its key, and the same under the ID with a suffix for its second key. A line that names no command is passed over, as one left by a newer Forge would be
void Shortcuts::ReadKeys()
{
    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(c_Path);
    if (!l_Text)
    {
        return;
    }

    std::size_t l_Start = 0;
    while (l_Start < l_Text->size())
    {
        const std::size_t l_End = std::min(l_Text->find('\n', l_Start), l_Text->size());
        std::string_view l_Line = std::string_view(*l_Text).substr(l_Start, l_End - l_Start);
        l_Start = l_End + 1;
        if (l_Line.ends_with('\r'))
        {
            l_Line.remove_suffix(1);
        }

        const std::size_t l_Equals = l_Line.find('=');
        if (l_Equals == std::string_view::npos)
        {
            continue;
        }

        std::string_view l_ID = l_Line.substr(0, l_Equals);
        const std::string_view l_Key = l_Line.substr(l_Equals + 1);
        const bool l_Alternate = l_ID.ends_with(c_AlternateSuffix);
        if (l_Alternate)
        {
            l_ID.remove_suffix(c_AlternateSuffix.size());
        }

        // A key that reads as none, and is not the word for none, is one this Forge does not know, and the command keeps its own
        const ImGuiKeyChord l_Chord = ParseChord(l_Key);
        Command* l_Command = FindCommand(l_ID);
        if (l_Command != nullptr && (l_Chord != ImGuiKey_None || l_Key == c_NoKey))
        {
            (l_Alternate ? l_Command->Alternate : l_Command->Chord) = l_Chord;
        }
    }
}

// Only the keys that are not as they were added, so a default changed in a later Forge reaches whoever left it alone
void Shortcuts::WriteKeys() const
{
    std::string l_Text;
    for (const Command& it_Command : m_Commands)
    {
        if (it_Command.Chord != it_Command.Default)
        {
            l_Text += std::format("{}={}\n", it_Command.ID, it_Command.Chord != ImGuiKey_None ? FormatChord(it_Command.Chord) : std::string(c_NoKey));
        }

        if (it_Command.Alternate != it_Command.DefaultAlternate)
        {
            l_Text += std::format("{}{}={}\n", it_Command.ID, c_AlternateSuffix, it_Command.Alternate != ImGuiKey_None ? FormatChord(it_Command.Alternate) : std::string(c_NoKey));
        }
    }

    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(c_Path, l_Text); !l_Written)
    {
        TR_WARN("Forge: the keyboard shortcuts could not be saved: {}", Trinity::ToString(l_Written.GetError()));
    }
}

// The modifiers held must be the key's own and no more, so Ctrl+S is not pressed with Ctrl+Shift+S, and the order of the list decides nothing between them
bool Shortcuts::Dispatch() const
{
    for (const Command& it_Command : m_Commands)
    {
        const ImGuiInputFlags l_Flags = it_Command.Repeats ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None;
        const bool l_Pressed = (it_Command.Chord != ImGuiKey_None && ImGui::IsKeyChordPressed(it_Command.Chord, l_Flags)) || (it_Command.Alternate != ImGuiKey_None && ImGui::IsKeyChordPressed(it_Command.Alternate, l_Flags));
        if (l_Pressed && (!it_Command.IsEnabled || it_Command.IsEnabled()))
        {
            it_Command.Run();

            return true;
        }
    }

    return false;
}