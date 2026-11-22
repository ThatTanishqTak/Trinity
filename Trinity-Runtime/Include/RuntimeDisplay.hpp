#pragma once

#include <Trinity.hpp>

// The player's window, as chosen through Screen, kept in Display.yaml. Alt+Enter toggles fullscreen
class RuntimeDisplay final : public Trinity::ScreenHost
{
public:
    // The game's window with its player's choice over it, read once the saves are mounted
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