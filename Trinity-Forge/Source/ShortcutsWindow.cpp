#include "ShortcutsWindow.hpp"

#include <Trinity/UI/Widgets.hpp>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr float c_KeyWidthInFonts = 12.0f;
    constexpr const char* c_KeyPopup = "Set Key###ForgeSetKey";

    bool ContainsIgnoringCase(std::string_view text, std::string_view part)
    {
        return !std::ranges::search(text, part, [](char left, char right) { return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right)); }).empty();
    }

    // A row of a group's table: what is done on the left, and the key that does it on the right
    struct Row
    {
        std::string_view Label;
        std::string Keys;
    };

    // The group's name over its rows, or nothing where the search left it none
    void DrawGroup(std::string_view name, const std::vector<Row>& rows)
    {
        if (rows.empty())
        {
            return;
        }

        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
        ImGui::SeparatorText(std::string(name).c_str());
        ImGui::PopFont();
        if (ImGui::BeginTable(std::string(name).c_str(), 2, ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * c_KeyWidthInFonts);
            for (const Row& it_Row : rows)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(it_Row.Label.data(), it_Row.Label.data() + it_Row.Label.size());
                ImGui::TableNextColumn();
                if (it_Row.Keys.empty())
                {
                    ImGui::TextDisabled("None");
                }
                else
                {
                    ImGui::TextUnformatted(it_Row.Keys.c_str());
                }
            }

            ImGui::EndTable();
        }
    }

    // Which of two commands on one key, one of a panel and one of none, runs where. The panel's runs while its panel has the focus, and the other everywhere else
    std::string DescribeSharing(const Shortcuts::Command& command, const Shortcuts::Command& sharer)
    {
        const Shortcuts::Command& l_Panel = command.Panel.empty() ? sharer : command;
        const Shortcuts::Command& l_Everywhere = command.Panel.empty() ? command : sharer;

        return std::format("{}: {} runs on it while the {} panel has the focus, and {}: {} everywhere else", l_Panel.Category, l_Panel.Label, l_Panel.Panel, l_Everywhere.Category, l_Everywhere.Label);
    }
}

ShortcutsWindow::ShortcutsWindow(EditorSession& session) : m_Session(session)
{

}

void ShortcutsWindow::Open()
{
    m_Open = true;
    m_FocusRequested = true;
}

void ShortcutsWindow::Draw()
{
    if (!m_Open)
    {
        return;
    }

    const float l_Font = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(l_Font * 34.0f, l_Font * 34.0f), ImGuiCond_FirstUseEver);
    const bool l_Focus = std::exchange(m_FocusRequested, false);
    if (l_Focus)
    {
        ImGui::SetNextWindowFocus();
    }

    if (!ImGui::Begin(std::format("{} Keyboard Shortcuts###ForgeShortcuts", Trinity::Icons::c_Book).c_str(), &m_Open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();

        return;
    }

    if (l_Focus)
    {
        ImGui::SetKeyboardFocusHere();
    }

    // Reset All at the right end, greyed while every key is as Forge came with it
    Shortcuts& l_Shortcuts = m_Session.GetShortcuts();
    const std::span<const Shortcuts::Command> l_Commands = l_Shortcuts.GetCommands();
    const char* l_ResetAll = "Reset All";
    const float l_ResetWidth = ImGui::CalcTextSize(l_ResetAll).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - l_ResetWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f));
    Trinity::Widgets::SearchField("##Search", "Search commands and keys", m_Search, ImGuiInputTextFlags_EscapeClearsAll);
    ImGui::SameLine();
    ImGui::BeginDisabled(std::ranges::all_of(l_Commands, &Shortcuts::Command::IsDefault));
    if (ImGui::Button(l_ResetAll))
    {
        l_Shortcuts.ResetAll();
    }

    ImGui::SetItemTooltip("Every command back on the key Forge came with");
    ImGui::EndDisabled();

    bool l_Any = false;
    if (ImGui::BeginChild("##Shortcuts"))
    {
        // The commands by category, each category where its first command is in the list
        std::vector<std::string_view> l_Categories;
        for (const Shortcuts::Command& it_Command : l_Commands)
        {
            if (std::ranges::find(l_Categories, it_Command.Category) == l_Categories.end())
            {
                l_Categories.push_back(it_Command.Category);
            }
        }

        for (const std::string_view it_Category : l_Categories)
        {
            DrawCommands(it_Category, l_Any);
        }

        // A group is found by what it is called too, so "scene" finds all of the Scene panel's keys
        const std::span<const Shortcuts::PanelKey> l_PanelKeys = Shortcuts::GetPanelKeys();
        std::vector<std::string_view> l_Panels;
        for (const Shortcuts::PanelKey& it_Key : l_PanelKeys)
        {
            if (std::ranges::find(l_Panels, it_Key.Panel) == l_Panels.end())
            {
                l_Panels.push_back(it_Key.Panel);
            }
        }

        for (const std::string_view it_Panel : l_Panels)
        {
            std::vector<Row> l_Rows;
            for (const Shortcuts::PanelKey& it_Key : l_PanelKeys)
            {
                if (it_Key.Panel == it_Panel && (m_Search.empty() || ContainsIgnoringCase(it_Panel, m_Search) || ContainsIgnoringCase(it_Key.Label, m_Search) || ContainsIgnoringCase(it_Key.Keys, m_Search)))
                {
                    l_Rows.push_back({ it_Key.Label, std::string(it_Key.Keys) });
                }
            }

            l_Any = l_Any || !l_Rows.empty();
            DrawGroup(std::format("{} (with the mouse)", it_Panel), l_Rows);
        }

        if (!l_Any)
        {
            ImGui::TextDisabled("No command or key is anything like %s", m_Search.c_str());
        }
    }

    ImGui::EndChild();

    // Opened here, at the window's own level, where the popup is begun
    if (std::exchange(m_OpenKeyPopup, false))
    {
        ImGui::OpenPopup(c_KeyPopup);
    }

    DrawKeyPopup();
    ImGui::End();
}

// A category's commands that the search leaves, each with its key as a button that asks for another, and where the key is not the one it came with, an arrow that puts that back
void ShortcutsWindow::DrawCommands(std::string_view category, bool& any)
{
    Shortcuts& l_Shortcuts = m_Session.GetShortcuts();
    std::vector<const Shortcuts::Command*> l_Shown;
    for (const Shortcuts::Command& it_Command : l_Shortcuts.GetCommands())
    {
        const std::string l_Keys = std::format("{} {}", Shortcuts::FormatChord(it_Command.Chord), Shortcuts::FormatChord(it_Command.Alternate));
        if (it_Command.Category == category && (m_Search.empty() || ContainsIgnoringCase(category, m_Search) || ContainsIgnoringCase(it_Command.Label, m_Search) || ContainsIgnoringCase(l_Keys, m_Search)))
        {
            l_Shown.push_back(&it_Command);
        }
    }

    if (l_Shown.empty())
    {
        return;
    }

    any = true;
    ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
    ImGui::SeparatorText(std::string(category).c_str());
    ImGui::PopFont();
    if (!ImGui::BeginTable(std::string(category).c_str(), 3, ImGuiTableFlags_RowBg))
    {
        return;
    }

    // Acted on once the rows are drawn, as a key given or put back changes other rows' keys too
    std::string_view l_Reset;
    ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * c_KeyWidthInFonts);
    ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    for (const Shortcuts::Command* it_Command : l_Shown)
    {
        ImGui::PushID(it_Command->ID.data(), it_Command->ID.data() + it_Command->ID.size());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(it_Command->Label.data(), it_Command->Label.data() + it_Command->Label.size());

        // Its first key is the one changed. A second, as Redo has, is told after it and stays until another command is given it
        ImGui::TableNextColumn();
        std::string l_Key = it_Command->Chord != ImGuiKey_None ? Shortcuts::FormatChord(it_Command->Chord) : std::string("None");
        if (it_Command->Alternate != ImGuiKey_None)
        {
            l_Key += std::format(", {}", Shortcuts::FormatChord(it_Command->Alternate));
        }

        if (it_Command->Chord == ImGuiKey_None)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }

        const bool l_Ask = ImGui::Button(std::format("{}###Key", l_Key).c_str(), ImVec2(-FLT_MIN, 0.0f));
        if (it_Command->Chord == ImGuiKey_None)
        {
            ImGui::PopStyleColor();
        }

        if (const Shortcuts::Command* l_Sharer = l_Shortcuts.FindSharer(it_Command->Chord, it_Command->ID))
        {
            ImGui::SetItemTooltip("%s", std::format("Click to press another key. {}", DescribeSharing(*it_Command, *l_Sharer)).c_str());
        }
        else
        {
            ImGui::SetItemTooltip("Click to press another key for it");
        }

        if (l_Ask)
        {
            m_Asking = std::string(it_Command->ID);
            m_Pressed = ImGuiKey_None;
            m_OpenKeyPopup = true;
        }

        ImGui::TableNextColumn();
        if (!it_Command->IsDefault())
        {
            if (ImGui::Button(Trinity::Icons::c_Undo, ImVec2(ImGui::GetFrameHeight(), 0.0f)))
            {
                l_Reset = it_Command->ID;
            }

            ImGui::SetItemTooltip("%s", std::format("Back to {}", it_Command->Default != ImGuiKey_None ? Shortcuts::FormatChord(it_Command->Default) : std::string("no key")).c_str());
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
    if (!l_Reset.empty())
    {
        l_Shortcuts.ResetKey(l_Reset);
    }
}

// Asks for the key by having it pressed, with whatever modifiers are held, and tells what the key does now before it is given. Escape leaves the command as it was, so it is the one key no command can have
void ShortcutsWindow::DrawKeyPopup()
{
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(c_KeyPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav))
    {
        return;
    }

    Shortcuts& l_Shortcuts = m_Session.GetShortcuts();
    const Shortcuts::Command* l_Command = l_Shortcuts.Find(m_Asking);
    if (l_Command == nullptr || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();

        return;
    }

    // Any key of the keyboard but the modifiers themselves, which are read as held with it
    for (int it_Key = ImGuiKey_NamedKey_BEGIN; it_Key < ImGuiKey_GamepadStart; ++it_Key)
    {
        const bool l_Modifier = it_Key >= ImGuiKey_LeftCtrl && it_Key <= ImGuiKey_RightSuper;
        if (!l_Modifier && ImGui::IsKeyPressed(static_cast<ImGuiKey>(it_Key), false))
        {
            m_Pressed = (ImGui::GetIO().KeyMods & ImGuiMod_Mask_) | it_Key;
        }
    }

    ImGui::TextUnformatted(std::format("Press the key for {}: {}", l_Command->Category, l_Command->Label).c_str());
    ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
    ImGui::TextUnformatted(m_Pressed != ImGuiKey_None ? Shortcuts::FormatChord(m_Pressed).c_str() : "...");
    ImGui::PopFont();

    // What giving it the key takes from, so nothing loses its key unseen
    if (const Shortcuts::Command* l_User = l_Shortcuts.FindUser(m_Pressed, l_Command->ID))
    {
        ImGui::TextColored(Trinity::Theme::GetColor(Trinity::ThemeColor::Warning), "%s", std::format("{} {}: {} Uses the shortcut", Trinity::Icons::c_Warning, l_User->Category, l_User->Label).c_str());
    }

    // Shared with a command of another panel, or of none, which both keep
    if (const Shortcuts::Command* l_Sharer = l_Shortcuts.FindSharer(m_Pressed, l_Command->ID))
    {
        ImGui::TextDisabled("%s", std::format("{} {}", Trinity::Icons::c_InfoCircle, DescribeSharing(*l_Command, *l_Sharer)).c_str());
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(m_Pressed == ImGuiKey_None);
    if (ImGui::Button("Use This Key"))
    {
        l_Shortcuts.SetKey(m_Asking, m_Pressed);
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(l_Command->Chord == ImGuiKey_None);
    if (ImGui::Button("No Key"))
    {
        l_Shortcuts.SetKey(m_Asking, ImGuiKey_None);
        ImGui::CloseCurrentPopup();
    }

    ImGui::SetItemTooltip("Leaves the command to its menu alone");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }

    ImGui::SetItemTooltip("Escape");
    ImGui::EndPopup();
}