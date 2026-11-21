#pragma once

#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <imgui.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// The engine log with filters, the console variables, and a command line, as Unity's Console
class ConsolePanel final : public Panel
{
public:
    using OpenLocation = std::function<void(std::string_view path, std::uint32_t line)>;

    ConsolePanel();

    // Brings the log forward showing only a build's own errors, until the filter is cleared
    void ShowBuildErrors(std::string label, std::uint64_t firstLine, std::uint64_t endLine, std::vector<std::string> errorLines);
    // Called with a line's file and line number when it is double-clicked, once the log is drawn
    void SetOpenLocation(OpenLocation open) { m_OpenLocation = std::move(open); }

    // Collapse, the Clear options, Error Pause and the detail pane's height, which imgui.ini keeps
    [[nodiscard]] bool ReadSetting(std::string_view key, std::string_view value);
    void WriteSettings(ImGuiTextBuffer& buffer) const;
    [[nodiscard]] bool IsClearingOnPlay() const { return m_ClearOnPlay; }
    [[nodiscard]] bool IsClearingOnBuild() const { return m_ClearOnBuild; }
    [[nodiscard]] bool IsPausingOnError() const { return m_ErrorPause; }

    // A build line's "path(line,column)", or else the first .NET stack frame that names its source
    [[nodiscard]] static std::optional<std::pair<std::string_view, std::uint32_t>> FindLocation(std::string_view text);
    // A level's colour, as the Console and the status bar show it, darker on a light theme
    [[nodiscard]] static ImVec4 GetLevelColor(Trinity::LogLevel level);
    // Green, as the menu bar shows a build that worked, darker on a light theme too
    [[nodiscard]] static ImVec4 GetSuccessColor();

protected:
    void OnImGuiRender() override;

private:
    void DrawLog(float footerHeight);
    void DrawLines(const Trinity::LogHistory::Reader& history, float height);
    void DrawDetail(const Trinity::LogHistory::Reader& history, float height);
    void DrawVariables(float footerHeight);
    void DrawCommandLine();
    void UpdateVisibleLines(const Trinity::LogHistory::Reader& history);
    void CopyVisibleLines(const Trinity::LogHistory::Reader& history) const;

    static int OnCommandLineEdit(ImGuiInputTextCallbackData* data);

    std::array<bool, 5> m_ShowLevels{ true, true, true, true, true };
    // How many lines of each level the whole history holds, as of the last frame
    std::array<std::size_t, 5> m_LevelCounts{};
    ImGuiTextFilter m_TextFilter;
    // The sequence of the line each row shows, and how many identical lines it stands for
    std::vector<std::uint64_t> m_VisibleLines;
    std::vector<std::uint32_t> m_VisibleCounts;
    // Each distinct line's row while collapsed, by its level and its text
    std::unordered_map<std::string, std::size_t> m_CollapsedRows;
    std::uint64_t m_FirstSequence = 0;
    std::uint64_t m_ScannedSequence = 0;
    std::optional<std::uint64_t> m_Selected;
    // The pane under the list, in lines of text
    float m_DetailLines = 6.0f;
    bool m_Collapse = false;
    bool m_ClearOnPlay = false;
    bool m_ClearOnBuild = false;
    bool m_ErrorPause = false;
    bool m_Rescan = true;
    bool m_AutoScroll = true;
    bool m_ScrollToBottom = false;

    std::vector<Trinity::ConsoleVariableBase*> m_Variables;

    OpenLocation m_OpenLocation;
    std::optional<std::pair<std::string, std::uint32_t>> m_PendingOpen;
    // A log file Open Log or Open Previous Log asked for, opened once the history is no longer held
    std::filesystem::path m_PendingLogFile;
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