#pragma once

#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <imgui.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The engine log with level and text filters, the console variables with an editor for each, and a command line with history
class ConsolePanel final : public Panel
{
public:
    ConsolePanel();

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

    std::array<char, 512> m_Command{};
    std::vector<std::string> m_CommandHistory;
    int m_HistoryPosition = -1;
};