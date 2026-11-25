#include "ShortcutsWindow.hpp"

#include <Trinity/UI/Widgets.hpp>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <format>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr float c_KeyWidthInFonts = 12.0f;

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

    ImGui::SetNextItemWidth(-FLT_MIN);
    Trinity::Widgets::SearchField("##Search", "Search commands and keys", m_Search, ImGuiInputTextFlags_EscapeClearsAll);

    // A group is listed by what it is called too, so "scene" finds all of the Scene panel's keys
    const auto a_Matches = [this](std::string_view group, std::string_view label, std::string_view keys) { return m_Search.empty() || ContainsIgnoringCase(group, m_Search) || ContainsIgnoringCase(label, m_Search) || ContainsIgnoringCase(keys, m_Search); };
    bool l_Any = false;
    if (ImGui::BeginChild("##Shortcuts"))
    {
        // The commands by category, each category where its first command is in the list
        const std::span<const Shortcuts::Command> l_Commands = m_Session.GetShortcuts().GetCommands();
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
            std::vector<Row> l_Rows;
            for (const Shortcuts::Command& it_Command : l_Commands)
            {
                std::string l_Keys = Shortcuts::FormatChord(it_Command.Chord);
                if (it_Command.Alternate != ImGuiKey_None)
                {
                    l_Keys += std::format(", {}", Shortcuts::FormatChord(it_Command.Alternate));
                }

                if (it_Command.Category == it_Category && a_Matches(it_Category, it_Command.Label, l_Keys))
                {
                    l_Rows.push_back({ it_Command.Label, std::move(l_Keys) });
                }
            }

            l_Any = l_Any || !l_Rows.empty();
            DrawGroup(it_Category, l_Rows);
        }

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
                if (it_Key.Panel == it_Panel && a_Matches(it_Panel, it_Key.Label, it_Key.Keys))
                {
                    l_Rows.push_back({ it_Key.Label, std::string(it_Key.Keys) });
                }
            }

            l_Any = l_Any || !l_Rows.empty();
            DrawGroup(std::format("{} (while it has the focus)", it_Panel), l_Rows);
        }

        if (!l_Any)
        {
            ImGui::TextDisabled("No command or key is anything like %s", m_Search.c_str());
        }
    }

    ImGui::EndChild();
    ImGui::End();
}