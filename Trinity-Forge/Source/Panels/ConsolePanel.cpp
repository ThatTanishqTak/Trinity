#include "Panels/ConsolePanel.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

namespace
{
    constexpr std::size_t c_CommandHistoryCapacity = 64;
    constexpr std::array<const char*, 5> c_LevelNames{ "Trace", "Info", "Warn", "Error", "Critical" };
    constexpr float c_MinimumDetailLines = 2.0f;
    constexpr float c_MaximumDetailLines = 60.0f;
    constexpr float c_MinimumListLines = 2.0f;
    constexpr std::size_t c_CountLimit = 999;
    constexpr const char* c_ClearOptionsPopup = "##ClearOptions";

    // The line's text up to its end of line, without a carriage return a tool left
    std::string_view TrimLine(std::string_view line)
    {
        return line.ends_with('\r') ? line.substr(0, line.size() - 1) : line;
    }

    // As BuildOutput writes a warning or an error, after its "Build: "
    std::optional<std::pair<std::string_view, std::uint32_t>> FindBuildLocation(std::string_view rest)
    {
        for (std::size_t l_Open = rest.find('('); l_Open != std::string_view::npos && l_Open > 0; l_Open = rest.find('(', l_Open + 1))
        {
            std::uint32_t l_Line = 0;
            const char* l_End = rest.data() + rest.size();
            const std::from_chars_result l_Parsed = std::from_chars(rest.data() + l_Open + 1, l_End, l_Line);
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
                return std::pair(rest.substr(0, l_Open), l_Line);
            }
        }

        return std::nullopt;
    }

    // A .NET stack frame that names its source, as an English runtime writes it, such as "   at Game.Player.OnUpdate() in C:\Game\Scripts\Player.cs:line 42"
    std::optional<std::pair<std::string_view, std::uint32_t>> FindFrameLocation(std::string_view line)
    {
        constexpr std::string_view c_At = "at ";
        constexpr std::string_view c_In = " in ";
        constexpr std::string_view c_LineMark = ":line ";
        const std::size_t l_Start = line.find_first_not_of(" \t");
        const std::size_t l_LineAt = line.rfind(c_LineMark);
        const std::size_t l_In = l_LineAt == std::string_view::npos ? std::string_view::npos : line.rfind(c_In, l_LineAt);
        if (l_Start == std::string_view::npos || !line.substr(l_Start).starts_with(c_At) || l_In == std::string_view::npos)
        {
            return std::nullopt;
        }

        std::uint32_t l_Number = 0;
        const char* l_End = line.data() + line.size();
        if (std::from_chars(line.data() + l_LineAt + c_LineMark.size(), l_End, l_Number).ec != std::errc() || l_Number == 0)
        {
            return std::nullopt;
        }

        const std::size_t l_Path = l_In + c_In.size();

        return std::pair(line.substr(l_Path, l_LineAt - l_Path), l_Number);
    }

    // Held down while its setting is on, as Unity's Console toggles are, and saved in imgui.ini when clicked. True as it is clicked
    bool ToggleButton(const char* label, bool& value, const char* tooltip)
    {
        const bool l_On = value;
        if (l_On)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }

        const bool l_Clicked = ImGui::Button(label);
        if (l_Clicked)
        {
            value = !value;
            ImGui::MarkIniSettingsDirty();
        }

        if (l_On)
        {
            ImGui::PopStyleColor();
        }

        ImGui::SetItemTooltip("%s", tooltip);

        return l_Clicked;
    }

    // A count as Unity's Console shows one, which stops at 999+
    std::string FormatCount(std::size_t count)
    {
        return count > c_CountLimit ? std::format("{}+", c_CountLimit) : std::to_string(count);
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
    m_VisibleCounts.reserve(Trinity::LogHistory::c_LineCapacity);
}

ImVec4 ConsolePanel::GetLevelColor(Trinity::LogLevel level)
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

std::optional<std::pair<std::string_view, std::uint32_t>> ConsolePanel::FindLocation(std::string_view text)
{
    constexpr std::string_view c_Prefix = "Build: ";
    if (text.starts_with(c_Prefix))
    {
        return FindBuildLocation(TrimLine(text.substr(0, text.find('\n')).substr(c_Prefix.size())));
    }

    for (std::size_t l_Start = 0; l_Start < text.size();)
    {
        const std::size_t l_End = std::min(text.find('\n', l_Start), text.size());
        if (const std::optional<std::pair<std::string_view, std::uint32_t>> l_Location = FindFrameLocation(TrimLine(text.substr(l_Start, l_End - l_Start))))
        {
            return l_Location;
        }

        l_Start = l_End + 1;
    }

    return std::nullopt;
}

bool ConsolePanel::ReadSetting(std::string_view key, std::string_view value)
{
    const bool l_On = value != "0";
    if (key == "ConsoleCollapse")
    {
        m_Collapse = l_On;
    }
    else if (key == "ConsoleClearOnPlay")
    {
        m_ClearOnPlay = l_On;
    }
    else if (key == "ConsoleClearOnBuild")
    {
        m_ClearOnBuild = l_On;
    }
    else if (key == "ConsoleErrorPause")
    {
        m_ErrorPause = l_On;
    }
    else if (key == "ConsoleDetailLines")
    {
        float l_Lines = 0.0f;
        if (std::from_chars(value.data(), value.data() + value.size(), l_Lines).ec == std::errc() && std::isfinite(l_Lines))
        {
            m_DetailLines = std::clamp(l_Lines, c_MinimumDetailLines, c_MaximumDetailLines);
        }
    }
    else
    {
        return false;
    }

    return true;
}

void ConsolePanel::WriteSettings(ImGuiTextBuffer& buffer) const
{
    const std::string l_Lines = std::format("ConsoleCollapse={}\nConsoleClearOnPlay={}\nConsoleClearOnBuild={}\nConsoleErrorPause={}\nConsoleDetailLines={}\n", m_Collapse ? 1 : 0, m_ClearOnPlay ? 1 : 0, m_ClearOnBuild ? 1 : 0, m_ErrorPause ? 1 : 0, m_DetailLines);
    buffer.append(l_Lines.c_str(), l_Lines.c_str() + l_Lines.size());
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

// Laid out as Unity's Console: its toolbar, the list, and the line selected in full under it, with a bar between the two to drag. The levels show last frame's counts, since the history is read after Clear, which cannot run while it is held. Nothing in here may log: the history stays locked while it is drawn, and this thread would wait on itself
void ConsolePanel::DrawLog(float footerHeight)
{
    const bool l_Clear = ImGui::Button("Clear");
    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::ArrowButton("##ClearOptionsButton", ImGuiDir_Down))
    {
        ImGui::OpenPopup(c_ClearOptionsPopup);
    }

    ImGui::SetItemTooltip("When the log clears itself");
    if (ImGui::BeginPopup(c_ClearOptionsPopup))
    {
        if (ImGui::MenuItem("Clear on Play", nullptr, &m_ClearOnPlay))
        {
            ImGui::MarkIniSettingsDirty();
        }

        ImGui::SetItemTooltip("Empties the log as a scene starts playing");
        if (ImGui::MenuItem("Clear on Build", nullptr, &m_ClearOnBuild))
        {
            ImGui::MarkIniSettingsDirty();
        }

        ImGui::SetItemTooltip("Empties the log as a C++ or C# build starts, unless the other is still building");
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ToggleButton("Collapse", m_Collapse, "Shows identical lines once, where the first was logged, with how many there are"))
    {
        m_Rescan = true;
    }

    ImGui::SameLine();
    ToggleButton("Error Pause", m_ErrorPause, "Pauses a playing scene when an error is logged");

    ToolbarSeparator();
    for (std::size_t it_Level = 0; it_Level < c_LevelNames.size(); ++it_Level)
    {
        if (it_Level > 0)
        {
            ImGui::SameLine();
        }

        const std::string l_Label = std::format("{} {}###Level{}", c_LevelNames[it_Level], FormatCount(m_LevelCounts[it_Level]), it_Level);
        ImGui::PushStyleColor(ImGuiCol_Text, GetLevelColor(static_cast<Trinity::LogLevel>(it_Level)));
        m_Rescan |= ImGui::Checkbox(l_Label.c_str(), &m_ShowLevels[it_Level]);
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

    // The pane keeps its height as the panel is resized, short of leaving the list fewer than two lines
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const float l_LineHeight = ImGui::GetTextLineHeightWithSpacing();
    const float l_SplitterHeight = std::max(l_Style.ItemSpacing.y, 1.0f);
    const float l_Height = ImGui::GetContentRegionAvail().y - footerHeight - l_SplitterHeight - 2.0f * l_Style.ItemSpacing.y;
    const float l_MaximumLines = std::max(l_Height / l_LineHeight - c_MinimumListLines, c_MinimumDetailLines);
    const float l_DetailHeight = std::min(m_DetailLines, l_MaximumLines) * l_LineHeight;

    DrawLines(l_History, std::max(l_Height - l_DetailHeight, 1.0f));

    // The bar between the list and the pane, dragged to size the pane, which imgui.ini keeps once it is let go
    const ImVec2 l_SplitterStart = ImGui::GetCursorScreenPos();
    const float l_SplitterWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    ImGui::InvisibleButton("##DetailSplitter", ImVec2(l_SplitterWidth, l_SplitterHeight));
    const bool l_SplitterHovered = ImGui::IsItemHovered();
    const bool l_SplitterActive = ImGui::IsItemActive();
    if (l_SplitterActive)
    {
        const float l_DetailBottom = l_SplitterStart.y + l_SplitterHeight + l_Style.ItemSpacing.y + l_DetailHeight;
        const float l_Lines = (l_DetailBottom - ImGui::GetIO().MousePos.y - l_SplitterHeight * 0.5f - l_Style.ItemSpacing.y) / l_LineHeight;
        m_DetailLines = std::clamp(l_Lines, c_MinimumDetailLines, std::min(l_MaximumLines, c_MaximumDetailLines));
    }

    if (ImGui::IsItemDeactivated())
    {
        ImGui::MarkIniSettingsDirty();
    }

    if (l_SplitterHovered || l_SplitterActive)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }

    const float l_SplitterY = std::floor(l_SplitterStart.y + l_SplitterHeight * 0.5f);
    const ImU32 l_SplitterColor = ImGui::GetColorU32(l_SplitterActive ? ImGuiCol_SeparatorActive : l_SplitterHovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(l_SplitterStart.x, l_SplitterY), ImVec2(l_SplitterStart.x + l_SplitterWidth, l_SplitterY), l_SplitterColor, l_SplitterHovered || l_SplitterActive ? 2.0f : 1.0f);

    DrawDetail(l_History, -footerHeight);
}

// A row a line of text, as Unity's Console shows them, so every row is as high as the next and the clipper skips those out of view: the first line of the text, "..." when more follow, and while collapsed how many times it was logged at the right. A click, or Up and Down, selects a row to be shown in full under the list, and a double-click opens the file and line it names
void ConsolePanel::DrawLines(const Trinity::LogHistory::Reader& history, float height)
{
    if (ImGui::BeginChild("##Lines", ImVec2(0.0f, height)))
    {
        const ImGuiStyle& l_Style = ImGui::GetStyle();
        const std::uint64_t l_First = history.GetCount() > 0 ? history[0].Sequence : 0;

        ImGuiListClipper l_Clipper;
        l_Clipper.Begin(static_cast<int>(m_VisibleLines.size()));
        while (l_Clipper.Step())
        {
            for (int it_Row = l_Clipper.DisplayStart; it_Row < l_Clipper.DisplayEnd; ++it_Row)
            {
                const std::size_t l_Row = static_cast<std::size_t>(it_Row);
                const std::uint64_t l_Sequence = m_VisibleLines[l_Row];
                const Trinity::LogEntry& l_Entry = history[static_cast<std::size_t>(l_Sequence - l_First)];
                const std::size_t l_LineEnd = l_Entry.Text.find('\n');
                const std::string_view l_Line = TrimLine(l_Entry.Text.substr(0, l_LineEnd));
                const bool l_More = l_LineEnd != std::string_view::npos && l_Entry.Text.find_first_not_of(" \t\r\n", l_LineEnd) != std::string_view::npos;
                const float l_Left = ImGui::GetCursorPosX();
                const float l_Right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

                // Sequences rather than rows name the rows, so keyboard focus stays on its line as lines come and go
                ImGui::PushID(static_cast<int>(l_Sequence));
                if (ImGui::Selectable("##Row", m_Selected == l_Sequence, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SelectOnNav))
                {
                    m_Selected = l_Sequence;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        if (const std::optional<std::pair<std::string_view, std::uint32_t>> l_Location = FindLocation(l_Entry.Text))
                        {
                            m_PendingOpen = std::pair(std::string(l_Location->first), l_Location->second);
                        }
                    }
                }

                const ImVec2 l_RowMin = ImGui::GetItemRectMin();
                const ImVec2 l_RowMax = ImGui::GetItemRectMax();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                {
                    if (const std::optional<std::pair<std::string_view, std::uint32_t>> l_Location = FindLocation(l_Entry.Text))
                    {
                        ImGui::SetTooltip("Double-click to open %.*s at line %u in the script editor", static_cast<int>(l_Location->first.size()), l_Location->first.data(), l_Location->second);
                    }
                }

                // A right-click selects the row too, as in Unity
                if (ImGui::BeginPopupContextItem("##RowMenu"))
                {
                    m_Selected = l_Sequence;
                    if (ImGui::MenuItem("Copy"))
                    {
                        const std::string l_Text = FormatPrefix(l_Entry) + std::string(l_Entry.Text);
                        ImGui::SetClipboardText(l_Text.c_str());
                    }

                    const std::optional<std::pair<std::string_view, std::uint32_t>> l_Location = FindLocation(l_Entry.Text);
                    if (ImGui::MenuItem("Open in Script Editor", nullptr, false, l_Location.has_value()))
                    {
                        m_PendingOpen = std::pair(std::string(l_Location->first), l_Location->second);
                    }

                    ImGui::EndPopup();
                }

                // The count, kept clear of the text
                const std::string l_Count = m_Collapse ? FormatCount(m_VisibleCounts[l_Row]) : std::string();
                const float l_CountWidth = m_Collapse ? ImGui::CalcTextSize(l_Count.c_str()).x + 2.0f * l_Style.FramePadding.x : 0.0f;
                const float l_TextRight = m_Collapse ? l_Right - l_CountWidth - l_Style.ItemSpacing.x : l_Right;

                ImGui::SameLine();
                ImGui::SetCursorPosX(l_Left);
                ImGui::PushClipRect(l_RowMin, ImVec2(l_TextRight, l_RowMax.y), true);
                const std::string l_Prefix = FormatPrefix(l_Entry);
                ImGui::TextDisabled("%s", l_Prefix.c_str());
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, GetLevelColor(l_Entry.Level));
                ImGui::TextUnformatted(l_Line.data(), l_Line.data() + l_Line.size());
                ImGui::PopStyleColor();
                if (l_More)
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::TextDisabled(" ...");
                }

                ImGui::PopClipRect();

                if (m_Collapse)
                {
                    const ImVec2 l_CountMin(l_Right - l_CountWidth, l_RowMin.y + 1.0f);
                    const ImVec2 l_CountMax(l_Right, l_RowMax.y - 1.0f);
                    const float l_TextY = l_CountMin.y + (l_CountMax.y - l_CountMin.y - ImGui::GetFontSize()) * 0.5f;
                    ImDrawList* l_DrawList = ImGui::GetWindowDrawList();
                    l_DrawList->AddRectFilled(l_CountMin, l_CountMax, ImGui::GetColorU32(ImGuiCol_FrameBgActive), (l_CountMax.y - l_CountMin.y) * 0.5f);
                    l_DrawList->AddText(ImVec2(l_CountMin.x + l_Style.FramePadding.x, l_TextY), ImGui::GetColorU32(ImGuiCol_Text), l_Count.c_str());
                }

                ImGui::PopID();
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

// The line selected in full, as Unity's Console shows it under the list. Each line of it naming a file and its line, such as a build's error or a frame of a C# stack trace, is a link that opens there
void ConsolePanel::DrawDetail(const Trinity::LogHistory::Reader& history, float height)
{
    if (ImGui::BeginChild("##Detail", ImVec2(0.0f, height)))
    {
        // Sequences are contiguous, so one before the first wraps around past the count
        const std::uint64_t l_First = history.GetCount() > 0 ? history[0].Sequence : 0;
        const Trinity::LogEntry* l_Entry = m_Selected && *m_Selected - l_First < history.GetCount() ? &history[static_cast<std::size_t>(*m_Selected - l_First)] : nullptr;
        if (l_Entry == nullptr)
        {
            ImGui::TextDisabled("Select a line to see it in full here");
        }
        else
        {
            ImGui::PushTextWrapPos(0.0f);
            const std::string l_Prefix = FormatPrefix(*l_Entry);
            ImGui::TextDisabled("%s", l_Prefix.c_str());

            const std::string_view l_Text = l_Entry->Text;
            for (std::size_t l_Start = 0; l_Start < l_Text.size();)
            {
                const std::size_t l_End = std::min(l_Text.find('\n', l_Start), l_Text.size());
                const std::string_view l_Line = TrimLine(l_Text.substr(l_Start, l_End - l_Start));
                const std::optional<std::pair<std::string_view, std::uint32_t>> l_Location = FindLocation(l_Line);
                ImGui::PushStyleColor(ImGuiCol_Text, l_Location ? ImGui::GetStyleColorVec4(ImGuiCol_TextLink) : l_Start == 0 ? GetLevelColor(l_Entry->Level) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
                ImGui::TextUnformatted(l_Line.data(), l_Line.data() + l_Line.size());
                ImGui::PopStyleColor();
                if (l_Location && ImGui::IsItemHovered())
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("Click to open %.*s at line %u in the script editor", static_cast<int>(l_Location->first.size()), l_Location->first.data(), l_Location->second);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        m_PendingOpen = std::pair(std::string(l_Location->first), l_Location->second);
                    }
                }

                l_Start = l_End + 1;
            }

            ImGui::PopTextWrapPos();
        }

        if (ImGui::BeginPopupContextWindow("##DetailMenu"))
        {
            if (ImGui::MenuItem("Copy", nullptr, false, l_Entry != nullptr))
            {
                const std::string l_Text = FormatPrefix(*l_Entry) + std::string(l_Entry->Text);
                ImGui::SetClipboardText(l_Text.c_str());
            }

            ImGui::EndPopup();
        }
    }

    ImGui::EndChild();
}

// Only lines added since the last frame are filtered, unless a filter changed. Sequences are contiguous within the history, so a sequence finds its line. Lines dropping out of the full history leave the front of the list, except while collapsed, where the counts they were in are found again from the start
void ConsolePanel::UpdateVisibleLines(const Trinity::LogHistory::Reader& history)
{
    if (history.GetCount() == 0)
    {
        m_VisibleLines.clear();
        m_VisibleCounts.clear();
        m_CollapsedRows.clear();
        m_LevelCounts.fill(0);
        m_Selected.reset();
        m_BuildLines.reset();
        m_Rescan = false;

        return;
    }

    const std::uint64_t l_First = history[0].Sequence;
    const std::uint64_t l_End = history[history.GetCount() - 1].Sequence + 1;
    const bool l_Dropped = l_First != std::exchange(m_FirstSequence, l_First);

    // A build's errors are shown alone only while some of its lines are left, as Clear on Build leaves none
    if (m_BuildLines && l_First >= m_BuildLines->second)
    {
        m_BuildLines.reset();
        m_Rescan = true;
    }

    // Each level's count, over the whole history whatever the filters show
    if (l_Dropped)
    {
        m_LevelCounts.fill(0);
    }

    for (std::uint64_t it_Sequence = l_Dropped ? l_First : std::max(m_ScannedSequence, l_First); it_Sequence < l_End; ++it_Sequence)
    {
        ++m_LevelCounts[std::to_underlying(history[static_cast<std::size_t>(it_Sequence - l_First)].Level)];
    }

    if (m_Rescan || (l_Dropped && m_Collapse))
    {
        m_VisibleLines.clear();
        m_VisibleCounts.clear();
        m_CollapsedRows.clear();
        m_ScannedSequence = l_First;
        m_Rescan = false;
    }
    else if (l_Dropped)
    {
        const std::ptrdiff_t l_Gone = std::ranges::lower_bound(m_VisibleLines, l_First) - m_VisibleLines.begin();
        m_VisibleLines.erase(m_VisibleLines.begin(), m_VisibleLines.begin() + l_Gone);
        m_VisibleCounts.erase(m_VisibleCounts.begin(), m_VisibleCounts.begin() + l_Gone);
    }

    for (std::uint64_t it_Sequence = std::max(m_ScannedSequence, l_First); it_Sequence < l_End; ++it_Sequence)
    {
        const Trinity::LogEntry& l_Entry = history[static_cast<std::size_t>(it_Sequence - l_First)];
        const bool l_InBuild = !m_BuildLines || (it_Sequence >= m_BuildLines->first && it_Sequence < m_BuildLines->second && l_Entry.Level >= Trinity::LogLevel::Error && std::ranges::binary_search(m_BuildErrors, l_Entry.Text, std::less<>()));
        if (!l_InBuild || !m_ShowLevels[std::to_underlying(l_Entry.Level)] || !m_TextFilter.PassFilter(l_Entry.Text.data(), l_Entry.Text.data() + l_Entry.Text.size()))
        {
            continue;
        }

        if (!m_Collapse)
        {
            m_VisibleLines.push_back(it_Sequence);
            m_VisibleCounts.push_back(1);

            continue;
        }

        // A line repeated adds to the row of its first, keyed by its level and its text
        std::string l_Key(1, static_cast<char>(std::to_underlying(l_Entry.Level)));
        l_Key += l_Entry.Text;
        const auto [a_Row, a_Added] = m_CollapsedRows.try_emplace(std::move(l_Key), m_VisibleLines.size());
        if (a_Added)
        {
            m_VisibleLines.push_back(it_Sequence);
            m_VisibleCounts.push_back(1);
        }
        else
        {
            ++m_VisibleCounts[a_Row->second];
        }
    }

    m_ScannedSequence = l_End;

    // The selection goes with its row, as a filter hides it or it drops out of the history. Rows stay in the order they were logged, collapsed or not
    if (m_Selected && !std::ranges::binary_search(m_VisibleLines, *m_Selected))
    {
        m_Selected.reset();
    }
}

// What the filters show, as the log file writes it, with how many times each line was logged while collapsed
void ConsolePanel::CopyVisibleLines(const Trinity::LogHistory::Reader& history) const
{
    if (history.GetCount() == 0)
    {
        return;
    }

    const std::uint64_t l_First = history[0].Sequence;

    std::string l_Text;
    for (std::size_t it_Row = 0; it_Row < m_VisibleLines.size(); ++it_Row)
    {
        const Trinity::LogEntry& l_Entry = history[static_cast<std::size_t>(m_VisibleLines[it_Row] - l_First)];
        l_Text += FormatPrefix(l_Entry);
        if (m_VisibleCounts[it_Row] > 1)
        {
            l_Text += std::format("({} times) ", m_VisibleCounts[it_Row]);
        }

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