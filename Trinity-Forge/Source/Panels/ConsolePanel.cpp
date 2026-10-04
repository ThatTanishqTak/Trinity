#include "Panels/ConsolePanel.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

namespace
{
    constexpr std::size_t c_CommandHistoryCapacity = 64;
    constexpr std::array<const char*, 5> c_LevelNames{ "Trace", "Info", "Warn", "Error", "Critical" };

    ImVec4 GetLevelColor(Trinity::LogLevel level)
    {
        switch (level)
        {
            case Trinity::LogLevel::Trace:
            {
                return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            }
            case Trinity::LogLevel::Warn:
            {
                return ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
            }
            case Trinity::LogLevel::Error:
            {
                return ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
            }
            case Trinity::LogLevel::Critical:
            {
                return ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
            }
            default:
            {
                return ImGui::GetStyleColorVec4(ImGuiCol_Text);
            }
        }
    }

    std::string FormatPrefix(const Trinity::LogEntry& entry)
    {
        return std::format("[{}] {:<7} {:<8} ", Trinity::Log::FormatTime(entry.Time), Trinity::ToString(entry.Channel), Trinity::ToString(entry.Level));
    }

    void ToolbarSeparator()
    {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }
}

ConsolePanel::ConsolePanel() : Panel("Console", Trinity::Icons::c_Terminal, DockSlot::Bottom)
{
    m_VisibleLines.reserve(Trinity::LogHistory::c_LineCapacity);
}

// The command line stays below whichever tab is open
void ConsolePanel::OnImGuiRender()
{
    // The separator and the command line below the tabs, each with the spacing after the item above it
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const float l_FooterHeight = l_Style.SeparatorSize + 2.0f * l_Style.ItemSpacing.y + ImGui::GetFrameHeight();

    if (ImGui::BeginTabBar("##ConsoleTabs"))
    {
        if (ImGui::BeginTabItem("Log"))
        {
            DrawLog(l_FooterHeight);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Variables"))
        {
            DrawVariables(l_FooterHeight);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Separator();
    DrawCommandLine();
}

// Nothing in here may log: the history stays locked while it is drawn, and this thread would wait on itself
void ConsolePanel::DrawLog(float footerHeight)
{
    for (std::size_t it_Level = 0; it_Level < c_LevelNames.size(); ++it_Level)
    {
        if (it_Level > 0)
        {
            ImGui::SameLine();
        }

        ImGui::PushStyleColor(ImGuiCol_Text, GetLevelColor(static_cast<Trinity::LogLevel>(it_Level)));
        m_Rescan |= ImGui::Checkbox(c_LevelNames[it_Level], &m_ShowLevels[it_Level]);
        ImGui::PopStyleColor();
    }

    ToolbarSeparator();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    if (ImGui::InputTextWithHint("##Filter", "Filter, such as vulkan,-trace", m_TextFilter.InputBuf, IM_COUNTOF(m_TextFilter.InputBuf)))
    {
        m_TextFilter.Build();
        m_Rescan = true;
    }

    ToolbarSeparator();
    ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
    ImGui::SameLine();
    const bool l_Clear = ImGui::Button("Clear");
    ImGui::SameLine();
    const bool l_Copy = ImGui::Button("Copy");

    if (l_Clear)
    {
        Trinity::LogHistory::Clear();
    }

    const Trinity::LogHistory::Reader l_History = Trinity::LogHistory::Read();
    UpdateVisibleLines(l_History);

    if (l_Copy)
    {
        CopyVisibleLines(l_History);
    }

    if (ImGui::BeginChild("##Lines", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
    {
        const std::uint64_t l_First = l_History.GetCount() > 0 ? l_History[0].Sequence : 0;

        ImGuiListClipper l_Clipper;
        l_Clipper.Begin(static_cast<int>(m_VisibleLines.size()));
        while (l_Clipper.Step())
        {
            for (int it_Row = l_Clipper.DisplayStart; it_Row < l_Clipper.DisplayEnd; ++it_Row)
            {
                const Trinity::LogEntry& l_Entry = l_History[static_cast<std::size_t>(m_VisibleLines[static_cast<std::size_t>(it_Row)] - l_First)];

                const std::string l_Prefix = FormatPrefix(l_Entry);
                ImGui::TextDisabled("%s", l_Prefix.c_str());
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, GetLevelColor(l_Entry.Level));
                ImGui::TextUnformatted(l_Entry.Text.data(), l_Entry.Text.data() + l_Entry.Text.size());
                ImGui::PopStyleColor();
            }
        }

        // Follows new lines only while already at the bottom, so scrolling up to read stays put
        if (m_ScrollToBottom || (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
        {
            ImGui::SetScrollHereY(1.0f);
        }

        m_ScrollToBottom = false;
    }

    ImGui::EndChild();
}

// Only lines added since the last frame are filtered, unless a filter changed. Sequences are contiguous within the history, so a sequence finds its line
void ConsolePanel::UpdateVisibleLines(const Trinity::LogHistory::Reader& history)
{
    if (history.GetCount() == 0)
    {
        m_VisibleLines.clear();
        m_Rescan = false;

        return;
    }

    const std::uint64_t l_First = history[0].Sequence;
    const std::uint64_t l_End = history[history.GetCount() - 1].Sequence + 1;
    if (m_Rescan)
    {
        m_VisibleLines.clear();
        m_ScannedSequence = l_First;
        m_Rescan = false;
    }

    m_VisibleLines.erase(m_VisibleLines.begin(), std::ranges::lower_bound(m_VisibleLines, l_First));

    for (std::uint64_t it_Sequence = std::max(m_ScannedSequence, l_First); it_Sequence < l_End; ++it_Sequence)
    {
        const Trinity::LogEntry& l_Entry = history[static_cast<std::size_t>(it_Sequence - l_First)];
        if (m_ShowLevels[std::to_underlying(l_Entry.Level)] && m_TextFilter.PassFilter(l_Entry.Text.data(), l_Entry.Text.data() + l_Entry.Text.size()))
        {
            m_VisibleLines.push_back(it_Sequence);
        }
    }

    m_ScannedSequence = l_End;
}

// What the filters show, as the log file writes it
void ConsolePanel::CopyVisibleLines(const Trinity::LogHistory::Reader& history) const
{
    if (history.GetCount() == 0)
    {
        return;
    }

    const std::uint64_t l_First = history[0].Sequence;

    std::string l_Text;
    for (const std::uint64_t it_Sequence : m_VisibleLines)
    {
        const Trinity::LogEntry& l_Entry = history[static_cast<std::size_t>(it_Sequence - l_First)];
        l_Text += FormatPrefix(l_Entry);
        l_Text += l_Entry.Text;
        l_Text += '\n';
    }

    ImGui::SetClipboardText(l_Text.c_str());
}

// Each change goes through ConsoleVariables::Set, which logs it. Read-only variables can only be set on the command line Trinity starts with
void ConsolePanel::DrawVariables(float footerHeight)
{
    m_Variables.clear();
    for (Trinity::ConsoleVariableBase* it_Variable = Trinity::ConsoleVariableBase::GetFirst(); it_Variable != nullptr; it_Variable = it_Variable->GetNext())
    {
        m_Variables.push_back(it_Variable);
    }

    std::ranges::sort(m_Variables, {}, &Trinity::ConsoleVariableBase::GetName);

    if (!ImGui::BeginChild("##Variables", ImVec2(0.0f, -footerHeight)))
    {
        ImGui::EndChild();

        return;
    }

    const ImGuiTableFlags l_Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##VariableTable", 4, l_Flags))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        for (Trinity::ConsoleVariableBase* it_Variable : m_Variables)
        {
            const std::string l_Name(it_Variable->GetName());
            const bool l_ReadOnly = Trinity::HasFlag(it_Variable->GetFlags(), Trinity::ConsoleVariableFlags::ReadOnly);

            ImGui::PushID(l_Name.c_str());
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(l_Name.c_str());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%.*s%s", static_cast<int>(it_Variable->GetDescription().size()), it_Variable->GetDescription().data(), l_ReadOnly ? "\nRead-only: set it with --set=name=value when Trinity starts" : "");
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::BeginDisabled(l_ReadOnly);
            switch (it_Variable->GetType())
            {
                case Trinity::ConsoleVariableType::Bool:
                {
                    bool l_Value = static_cast<Trinity::ConsoleVariable<bool>*>(it_Variable)->Get();
                    if (ImGui::Checkbox("##Value", &l_Value))
                    {
                        Trinity::ConsoleVariables::Set(l_Name, l_Value ? "true" : "false");
                    }

                    break;
                }
                case Trinity::ConsoleVariableType::Int:
                {
                    int l_Value = static_cast<Trinity::ConsoleVariable<std::int32_t>*>(it_Variable)->Get();
                    if (ImGui::InputInt("##Value", &l_Value, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        Trinity::ConsoleVariables::Set(l_Name, std::to_string(l_Value));
                    }

                    break;
                }
                case Trinity::ConsoleVariableType::Float:
                {
                    float l_Value = static_cast<Trinity::ConsoleVariable<float>*>(it_Variable)->Get();
                    if (ImGui::InputFloat("##Value", &l_Value, 0.0f, 0.0f, "%g", ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        Trinity::ConsoleVariables::Set(l_Name, std::format("{}", l_Value));
                    }

                    break;
                }
                case Trinity::ConsoleVariableType::String:
                {
                    std::array<char, 256> l_Value{};
                    const std::string l_Current = it_Variable->ToString();
                    std::ranges::copy_n(l_Current.begin(), static_cast<std::ptrdiff_t>(std::min(l_Current.size(), l_Value.size() - 1)), l_Value.begin());
                    if (ImGui::InputText("##Value", l_Value.data(), l_Value.size(), ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        Trinity::ConsoleVariables::Set(l_Name, l_Value.data());
                    }

                    break;
                }
            }

            ImGui::EndDisabled();

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", it_Variable->DefaultToString().c_str());

            ImGui::TableNextColumn();
            const std::string_view l_Type = Trinity::ToString(it_Variable->GetType());
            ImGui::TextDisabled("%.*s%s", static_cast<int>(l_Type.size()), l_Type.data(), l_ReadOnly ? ", read-only" : "");

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

// Enter runs the line through ConsoleVariables::Execute, and Up and Down walk back through earlier lines
void ConsolePanel::DrawCommandLine()
{
    const ImGuiInputTextFlags l_Flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_CallbackHistory;

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##Command", "help, a variable name, or a name and a value", m_Command.data(), m_Command.size(), l_Flags, &ConsolePanel::OnCommandLineEdit, this))
    {
        const std::string l_Command(m_Command.data());
        if (!l_Command.empty())
        {
            Trinity::ConsoleVariables::Execute(l_Command);

            std::erase(m_CommandHistory, l_Command);
            m_CommandHistory.push_back(l_Command);
            if (m_CommandHistory.size() > c_CommandHistoryCapacity)
            {
                m_CommandHistory.erase(m_CommandHistory.begin());
            }
        }

        m_Command.fill('\0');
        m_HistoryPosition = -1;
        m_ScrollToBottom = true;

        // Enter takes the keyboard away from the field, so it is given back for the next line
        ImGui::SetKeyboardFocusHere(-1);
    }
}

int ConsolePanel::OnCommandLineEdit(ImGuiInputTextCallbackData* data)
{
    ConsolePanel& l_Panel = *static_cast<ConsolePanel*>(data->UserData);
    if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || l_Panel.m_CommandHistory.empty())
    {
        return 0;
    }

    const int l_Count = static_cast<int>(l_Panel.m_CommandHistory.size());
    const int l_Previous = l_Panel.m_HistoryPosition;
    if (data->EventKey == ImGuiKey_UpArrow)
    {
        l_Panel.m_HistoryPosition = l_Panel.m_HistoryPosition == -1 ? l_Count - 1 : std::max(l_Panel.m_HistoryPosition - 1, 0);
    }
    else if (data->EventKey == ImGuiKey_DownArrow && l_Panel.m_HistoryPosition != -1)
    {
        l_Panel.m_HistoryPosition = l_Panel.m_HistoryPosition + 1 < l_Count ? l_Panel.m_HistoryPosition + 1 : -1;
    }

    if (l_Previous != l_Panel.m_HistoryPosition)
    {
        const std::string_view l_Line = l_Panel.m_HistoryPosition >= 0 ? std::string_view(l_Panel.m_CommandHistory[static_cast<std::size_t>(l_Panel.m_HistoryPosition)]) : std::string_view{};
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, l_Line.data(), l_Line.data() + l_Line.size());
    }

    return 0;
}