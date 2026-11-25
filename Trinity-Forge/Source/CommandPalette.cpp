#include "CommandPalette.hpp"

#include <Trinity/UI/Widgets.hpp>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <format>
#include <functional>
#include <span>
#include <string_view>
#include <utility>

namespace
{
    constexpr const char* c_Popup = "##ForgeCommandPalette";
    constexpr float c_WidthInFonts = 36.0f;
    // The list is this many rows high at most, and scrolls past them
    constexpr float c_Rows = 12.0f;
    // How far down the window it opens, as a share of the window's height
    constexpr float c_Top = 0.18f;
    constexpr std::size_t c_RecentLimit = 5;

    bool EqualsIgnoringCase(char left, char right)
    {
        return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
    }

    bool ContainsIgnoringCase(std::string_view text, std::string_view part)
    {
        return !std::ranges::search(text, part, EqualsIgnoringCase).empty();
    }

    bool StartsWithIgnoringCase(std::string_view text, std::string_view part)
    {
        return text.size() >= part.size() && std::ranges::equal(text.substr(0, part.size()), part, EqualsIgnoringCase);
    }
}

CommandPalette::CommandPalette(EditorSession& session) : m_Session(session)
{

}

// Each word typed has to be somewhere in the command's category and name, in any order. A name that begins with what was typed comes first, and the rest stay in the registry's order
std::vector<const Shortcuts::Command*> CommandPalette::FindMatches() const
{
    const std::span<const Shortcuts::Command> l_Commands = m_Session.GetShortcuts().GetCommands();
    std::vector<const Shortcuts::Command*> l_Matches;
    if (m_Search.empty())
    {
        for (const std::string& it_Recent : m_Recent)
        {
            if (const Shortcuts::Command* l_Command = m_Session.GetShortcuts().Find(it_Recent))
            {
                l_Matches.push_back(l_Command);
            }
        }
    }

    for (const Shortcuts::Command& it_Command : l_Commands)
    {
        const std::string l_Text = std::format("{} {}", it_Command.Category, it_Command.Label);
        bool l_Found = std::ranges::find(l_Matches, &it_Command) == l_Matches.end();
        std::string_view l_Words = m_Search;
        while (l_Found && !l_Words.empty())
        {
            const std::size_t l_Space = l_Words.find(' ');
            const std::string_view l_Word = l_Words.substr(0, l_Space);
            l_Found = l_Word.empty() || ContainsIgnoringCase(l_Text, l_Word);
            l_Words = l_Space != std::string_view::npos ? l_Words.substr(l_Space + 1) : std::string_view();
        }

        if (l_Found)
        {
            l_Matches.push_back(&it_Command);
        }
    }

    if (!m_Search.empty())
    {
        std::ranges::stable_partition(l_Matches, [this](const Shortcuts::Command* command) { return StartsWithIgnoringCase(command->Label, m_Search); });
    }

    return l_Matches;
}

// A popup under the top of the main window, so a click anywhere else lets it go. The arrows move the choice while the search keeps the keyboard, and what is chosen runs once the popup has closed, since it may open a window or a popup of its own
void CommandPalette::Draw()
{
    if (std::exchange(m_OpenRequested, false))
    {
        m_Search.clear();
        m_Chosen = 0;
        ImGui::OpenPopup(c_Popup);
    }

    const ImGuiViewport& l_Viewport = *ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(l_Viewport.WorkPos.x + l_Viewport.WorkSize.x * 0.5f, l_Viewport.WorkPos.y + l_Viewport.WorkSize.y * c_Top), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * c_WidthInFonts, 0.0f));
    if (!ImGui::BeginPopup(c_Popup, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }

    // At the first press, where the search alone would only give up the keyboard and leave the popup open
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();

        return;
    }

    const std::string l_Before = m_Search;
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool l_Entered = Trinity::Widgets::SearchField("##Search", "Type a command", m_Search, ImGuiInputTextFlags_EnterReturnsTrue);
    if (m_Search != l_Before)
    {
        m_Chosen = 0;
        m_ScrollToChosen = true;
    }

    const std::vector<const Shortcuts::Command*> l_Matches = FindMatches();
    m_Chosen = std::min(m_Chosen, l_Matches.empty() ? 0 : l_Matches.size() - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !l_Matches.empty())
    {
        m_Chosen = (m_Chosen + 1) % l_Matches.size();
        m_ScrollToChosen = true;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !l_Matches.empty())
    {
        m_Chosen = (m_Chosen + l_Matches.size() - 1) % l_Matches.size();
        m_ScrollToChosen = true;
    }

    const Shortcuts::Command* l_Run = nullptr;
    const float l_RowHeight = ImGui::GetTextLineHeightWithSpacing();
    const float l_Height = l_RowHeight * std::min(static_cast<float>(std::max<std::size_t>(l_Matches.size(), 1)), c_Rows);
    if (ImGui::BeginChild("##Commands", ImVec2(0.0f, l_Height + ImGui::GetStyle().ItemSpacing.y)))
    {
        if (l_Matches.empty())
        {
            ImGui::TextDisabled("No command is called anything like %s", m_Search.c_str());
        }

        // The mouse takes the choice only as it moves, so the arrows are not undone by where it happens to rest
        const bool l_MouseMoved = ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f;
        for (std::size_t it_Index = 0; it_Index < l_Matches.size(); ++it_Index)
        {
            const Shortcuts::Command& l_Command = *l_Matches[it_Index];
            const bool l_Ready = !l_Command.IsEnabled || l_Command.IsEnabled();
            const float l_Right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::PushID(l_Command.ID.c_str());
            ImGui::BeginDisabled(!l_Ready);
            if (ImGui::Selectable(std::format("{}: {}", l_Command.Category, l_Command.Label).c_str(), it_Index == m_Chosen, ImGuiSelectableFlags_AllowOverlap))
            {
                l_Run = &l_Command;
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && l_MouseMoved)
            {
                m_Chosen = it_Index;
            }

            if (it_Index == m_Chosen && std::exchange(m_ScrollToChosen, false))
            {
                ImGui::SetScrollHereY();
            }

            // Its key at the row's right end, for the next time
            if (const std::string l_Key = Shortcuts::FormatChord(l_Command.Chord); !l_Key.empty())
            {
                ImGui::SameLine(l_Right - ImGui::CalcTextSize(l_Key.c_str()).x);
                ImGui::TextDisabled("%s", l_Key.c_str());
            }

            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

    ImGui::EndChild();
    if (l_Entered && !l_Matches.empty() && (!l_Matches[m_Chosen]->IsEnabled || l_Matches[m_Chosen]->IsEnabled()))
    {
        l_Run = l_Matches[m_Chosen];
    }

    std::function<void()> l_Action;
    if (l_Run != nullptr)
    {
        std::erase(m_Recent, l_Run->ID);
        m_Recent.insert(m_Recent.begin(), l_Run->ID);
        m_Recent.resize(std::min(m_Recent.size(), c_RecentLimit));
        l_Action = l_Run->Run;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    if (l_Action)
    {
        l_Action();
    }
}