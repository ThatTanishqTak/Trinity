#pragma once

#if !defined(TR_DISTRIBUTION)
#include <Trinity.hpp>

#include <cstddef>
#include <cstdint>

struct ImGuiViewport;

// A Development player's overlay, drawn by ImGui over the game, which never takes its input: a Development Build mark always in the bottom right corner, with how many errors and warnings the game has logged since it started once there are any, and on F3 the frame's statistics and the sound's in the top left and the last log lines along the bottom, errors red and warnings yellow. F4 moves RmlUi's debugger on through the game's documents, which the mark names it as inspecting. F3 and F4 are taken from the game. None of it is in a Distribution player
class DevelopmentOverlay final : public Trinity::Layer
{
public:
    static constexpr Trinity::KeyCode c_StatsKey = Trinity::KeyCode::TR_F3;
    static constexpr Trinity::KeyCode c_DebuggerKey = Trinity::KeyCode::TR_F4;
    // How many of the last log lines F3 shows
    static constexpr std::size_t c_LogLines = 12;

    DevelopmentOverlay();

    void OnEvent(Trinity::Event& event) override;
    void OnImGuiRender() override;

    [[nodiscard]] bool IsShowingStats() const { return m_ShowStats; }
    [[nodiscard]] std::uint32_t GetErrorCount() const { return m_Errors; }
    [[nodiscard]] std::uint32_t GetWarningCount() const { return m_Warnings; }

private:
    // The lines logged since the last frame, counted by their level
    void CountLog();
    void DrawMark(const ImGuiViewport& viewport) const;
    void DrawStats(const ImGuiViewport& viewport) const;
    void DrawLog(const ImGuiViewport& viewport) const;

    bool m_ShowStats = false;
    std::uint64_t m_NextLine = 0;
    std::uint32_t m_Errors = 0;
    std::uint32_t m_Warnings = 0;
};
#endif