#include "Panels/ConsolePanel.hpp"

#include <algorithm>
#include <charconv>
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

std::optional<std::pair<std::string_view, std::uint32_t>> ConsolePanel::FindLocation(std::string_view text)
{
    constexpr std::string_view c_Prefix = "Build: ";
    if (!text.starts_with(c_Prefix))
    {
        return std::nullopt;
    }

    const std::string_view l_Rest = text.substr(c_Prefix.size());
    for (std::size_t l_Open = l_Rest.find('('); l_Open != std::string_view::npos && l_Open > 0; l_Open = l_Rest.find('(', l_Open + 1))
    {
        std::uint32_t l_Line = 0;
        const char* l_End = l_Rest.data() + l_Rest.size();
        const std::from_chars_result l_Parsed = std::from_chars(l_Rest.data() + l_Open + 1, l_End, l_Line);
        if (l_Parsed.ec != std::errc() || l_Line == 0)
        {
            continue;
        }

        const char* l_After = l_Parsed.ptr;
        if (l_After != l_End && *l_After == ',')
        {
            std::uint32_t l_Column = 0;
            const std::from_chars_result l_ColumnParsed = std::from_chars(l_After + 1, l_End, l_Column);
            l_After = l_ColumnParsed.ec == std::errc() ? l_ColumnParsed.ptr : l_After;
        }

        if (std::string_view(l_After, static_cast<std::size_t>(l_End - l_After)).starts_with("):"))
        {
            return std::pair(l_Rest.substr(0, l_Open), l_Line);
        }
    }

    return std::nullopt;
}

void ConsolePanel::ShowBuildErrors(std::string label, std::uint64_t firstLine, std::uint64_t endLine, std::vector<std::string> errorLines)
{
    std::ranges::sort(errorLines);
    m_BuildErrors = std::move(errorLines);
    m_BuildLines = std::pair(firstLine, endLine);
    m_BuildLabel = std::move(label);
    m_Rescan = true;
    m_ScrollToTop = true;
    m_SelectLog = true;
    SetOpen(true);
    RequestFocus();
}

// The command line stays below whichever tab is open
void ConsolePanel::OnImGuiRender()
{
    // The separator and the command line below the tabs, each with the spacing after the item above it
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const float l_FooterHeight = l_Style.SeparatorSize + 2.0f * l_Style.ItemSpacing.y + ImGui::GetFrameHeight();

    if (ImGui::BeginTabBar("##ConsoleTabs"))
    {
        if (ImGui::BeginTabItem("Log", nullptr, std::exchange(m_SelectLog, false) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
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

    // Once the history is no longer held, since opening a file logs
    if (const std::optional<std::pair<std::string, std::uint32_t>> l_Open = std::exchange(m_PendingOpen, std::nullopt); l_Open && m_OpenLocation)
    {
        m_OpenLocation(l_Open->first, l_Open->second);
    }

    if (const std::filesystem::path l_Log = std::exchange(m_PendingLogFile, {}); !l_Log.empty())
    {
        if (const Trinity::Expected<void, std::string> l_Opened = Trinity::Process::OpenWithSystem(l_Log); !l_Opened)
        {
            const std::u8string l_Name = l_Log.u8string();
            TR_ERROR("Forge: {} could not be opened: {}", std::string_view(reinterpret_cast<const char*>(l_Name.data()), l_Name.size()), l_Opened.GetError());
        }
    }
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

    // A build's errors, shown alone until this is clicked
    if (m_BuildLines)
    {
        ToolbarSeparator();
        if (ImGui::Button(std::format("{} {} errors only###BuildErrors", Trinity::Icons::c_TimesCircle, m_BuildLabel).c_str()))
        {
            m_BuildLines.reset();
            m_Rescan = true;
            m_ScrollToBottom = true;
        }

        ImGui::SetItemTooltip("Showing only the errors of the last %s build. Click to show the whole log again", m_BuildLabel.c_str());
    }

    ToolbarSeparator();
    ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
    ImGui::SameLine();
    const bool l_Clear = ImGui::Button("Clear");
    ImGui::SameLine();
    const bool l_Copy = ImGui::Button("Copy");

    // The log files, as Unity's Console menu opens Editor.log: this run's, and the last run's, which is what a crash leaves
    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Log Files", Trinity::Icons::c_File).c_str()))
    {
        ImGui::OpenPopup("##LogFiles");
    }

    if (ImGui::BeginPopup("##LogFiles"))
    {
        const std::filesystem::path& l_File = Trinity::Log::GetFilePath();
        const std::filesystem::path& l_Previous = Trinity::Log::GetPreviousFilePath();
        if (ImGui::MenuItem("Open Log", nullptr, false, !l_File.empty()))
        {
            m_PendingLogFile = l_File;
        }

        if (ImGui::MenuItem("Open Previous Log", nullptr, false, !l_Previous.empty()))
        {
            m_PendingLogFile = l_Previous;
        }

        ImGui::Separator();
        const std::u8string l_Folder = l_File.parent_path().u8string();
        ImGui::TextDisabled("%s", l_File.empty() ? "File logging is off, as the log says" : reinterpret_cast<const char*>(l_Folder.c_str()));
        ImGui::EndPopup();
    }

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
                if (ImGui::IsItemHovered())
                {
                    if (const std::optional<std::pair<std::string_view, std::uint32_t>> l_Location = FindLocation(l_Entry.Text))
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        ImGui::SetItemTooltip("Double-click to open %.*s at line %u in the script editor", static_cast<int>(l_Location->first.size()), l_Location->first.data(), l_Location->second);
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            m_PendingOpen = std::pair(std::string(l_Location->first), l_Location->second);
                        }
                    }
                }
            }
        }

        // A build's errors start from the first. Otherwise new lines are followed only while already at the bottom, so scrolling up to read stays put
        if (std::exchange(m_ScrollToTop, false))
        {
            ImGui::SetScrollY(0.0f);
        }
        else if (m_ScrollToBottom || (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
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
        const bool l_InBuild = !m_BuildLines || (it_Sequence >= m_BuildLines->first && it_Sequence < m_BuildLines->second && l_Entry.Level >= Trinity::LogLevel::Error && std::ranges::binary_search(m_BuildErrors, l_Entry.Text, std::less<>()));
        if (l_InBuild && m_ShowLevels[std::to_underlying(l_Entry.Level)] && m_TextFilter.PassFilter(l_Entry.Text.data(), l_Entry.Text.data() + l_Entry.Text.size()))
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