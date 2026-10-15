#pragma once

#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <imgui.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The engine log with level and text filters, the console variables with an editor for each, and a command line with history. A line naming a file and its line, as a build's warnings and errors do, opens there on a double-click
class ConsolePanel final : public Panel
{
public:
    using OpenLocation = std::function<void(std::string_view path, std::uint32_t line)>;

    ConsolePanel();

    // Brings the log to the front showing only a build's errors, from its first, until the filter is cleared: the lines from first up to end logged at error that are among the build's own, since another build running beside it logs between them
    void ShowBuildErrors(std::string label, std::uint64_t firstLine, std::uint64_t endLine, std::vector<std::string> errorLines);
    // Called with a line's file and line number when it is double-clicked, once the log is drawn
    void SetOpenLocation(OpenLocation open) { m_OpenLocation = std::move(open); }

    // The file and line a build's line names, as BuildOutput writes them, such as "Build: Scripts/Player.cs(12,5): error ...". The first parenthesis holding a line number, and a column or not, ends the path, so a folder such as Program Files (x86) stays in it
    [[nodiscard]] static std::optional<std::pair<std::string_view, std::uint32_t>> FindLocation(std::string_view text);

protected:
    void OnImGuiRender() override;

private:
    void DrawLog(float footerHeight);
    void DrawVariables(float footerHeight);
    void DrawCommandLine();
    void UpdateVisibleLines(const Trinity::LogHistory::Reader& history);
    void CopyVisibleLines(const Trinity::LogHistory::Reader& history) const;

    static int OnCommandLineEdit(ImGuiInputTextCallbackData* data);

    std::array<bool, 5> m_ShowLevels{ true, true, true, true, true };
    ImGuiTextFilter m_TextFilter;
    std::vector<std::uint64_t> m_VisibleLines;
    std::uint64_t m_ScannedSequence = 0;
    bool m_Rescan = true;
    bool m_AutoScroll = true;
    bool m_ScrollToBottom = false;

    std::vector<Trinity::ConsoleVariableBase*> m_Variables;

    OpenLocation m_OpenLocation;
    std::optional<std::pair<std::string, std::uint32_t>> m_PendingOpen;
    std::optional<std::pair<std::uint64_t, std::uint64_t>> m_BuildLines;
    // Sorted, to be looked up as each line is filtered
    std::vector<std::string> m_BuildErrors;
    std::string m_BuildLabel;
    bool m_ScrollToTop = false;
    bool m_SelectLog = false;

    std::array<char, 512> m_Command{};
    std::vector<std::string> m_CommandHistory;
    int m_HistoryPosition = -1;
};