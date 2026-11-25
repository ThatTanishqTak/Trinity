#include "Shortcuts.hpp"

#include <Trinity.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <utility>

namespace
{
    // The keys ImGui names by a word and a keyboard marks with a sign
    struct KeySign
    {
        ImGuiKey Key = ImGuiKey_None;
        std::string_view Sign;
    };

    constexpr std::array<KeySign, 11> c_KeySigns{ {
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
    constexpr std::array<std::pair<ImGuiKeyChord, std::string_view>, 4> c_Mods{ { { ImGuiMod_Ctrl, "Ctrl+" },{ ImGuiMod_Alt, "Alt+" },{ ImGuiMod_Shift, "Shift+" },{ ImGuiMod_Super, "Super+" } } };
    for (const auto& [it_Mod, it_Name] : c_Mods)
    {
        if ((chord & it_Mod) != 0)
        {
            l_Text += it_Name;
        }
    }

    const auto a_Sign = std::ranges::find(c_KeySigns, l_Key, &KeySign::Key);
    l_Text += a_Sign != c_KeySigns.end() ? a_Sign->Sign : std::string_view(ImGui::GetKeyName(l_Key));

    return l_Text;
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