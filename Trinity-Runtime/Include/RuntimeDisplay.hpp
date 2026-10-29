#pragma once

#include <Trinity.hpp>

// The player's window, as its player and the game's scripts choose it through Screen: framed at a size, or borderless over its monitor at the monitor's own size, with vsync or without. Alt+Enter switches between the two. A change applies once the frame's update ends, as Unity's do. The choice, with the monitor the window is on and where it was while windowed, is kept in Display.yaml in the saves whenever it changes and as the player closes, so the next start opens as this one left it. A headless player is on no monitor, so where its window is is not kept
class RuntimeDisplay final : public Trinity::ScreenHost
{
public:
    // The game's window with its player's choice over it, read once the saves are mounted, before the window is made
    [[nodiscard]] static Trinity::DisplaySettings Load(const Trinity::WindowSettings& game);
    static void Configure(const Trinity::DisplaySettings& settings, Trinity::WindowSpecification& window);

    // The screen Screen reads and changes from now, with vsync as the settings have it
    explicit RuntimeDisplay(Trinity::DisplaySettings settings);
    // Keeps the choice, then leaves Screen with no host
    ~RuntimeDisplay() override;

    RuntimeDisplay(const RuntimeDisplay&) = delete;
    RuntimeDisplay& operator=(const RuntimeDisplay&) = delete;

    // Alt+Enter, with either Alt and either Enter, switches the mode
    void OnEvent(Trinity::Event& event);
    // Once the frame's update has ended
    void Apply();

    [[nodiscard]] std::uint32_t GetWidth() const override;
    [[nodiscard]] std::uint32_t GetHeight() const override;
    [[nodiscard]] Trinity::FullScreenMode GetFullScreenMode() const override { return m_Settings.Window.Mode; }
    [[nodiscard]] bool IsVSync() const override { return m_Settings.Window.VSync; }
    void Request(const Trinity::ScreenRequest& request) override;

private:
    // The windowed size and place the player has left the window at, and the monitor it is on
    void Follow();
    void Save();

    Trinity::DisplaySettings m_Settings;
    // As Display.yaml last held it, so nothing unchanged is written
    Trinity::DisplaySettings m_Saved;
    Trinity::ScreenRequest m_Request;
    bool m_Requested = false;
};