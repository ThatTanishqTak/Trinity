#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Where a panel goes in the default layout. A Floating one is in no dock there and is closed until asked for, opening as a window of its own in the middle, which can be docked as any other
enum class DockSlot : std::uint8_t
{
    Centre,
    Left,
    Right,
    Bottom,
    Floating
};

// A dockable Forge window. ImGui knows it by its ID alone, so its icon and title can change
class Panel
{
public:
    Panel(std::string_view title, std::string_view icon, DockSlot slot, std::string_view id = {});
    virtual ~Panel() = default;

    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    void Draw();

    [[nodiscard]] const std::string& GetTitle() const { return m_Title; }
    // What imgui.ini knows the panel by
    [[nodiscard]] const std::string& GetSettingsID() const { return m_SettingsID; }
    [[nodiscard]] const std::string& GetWindowName() const { return m_WindowName; }
    [[nodiscard]] const std::string& GetMenuLabel() const { return m_MenuLabel; }
    [[nodiscard]] DockSlot GetSlot() const { return m_Slot; }

    [[nodiscard]] bool IsOpen() const { return m_Open; }
    void SetOpen(bool open);
    // Brings the panel to the front the next time it is drawn, selecting its tab where docked
    void RequestFocus() { m_FocusRequested = true; }
    [[nodiscard]] bool IsFocusRequested() const { return m_FocusRequested; }
    // Whether the mouse was over the panel, and whether it had the focus, as it was last drawn. Neither for one that is closed or was not drawn
    [[nodiscard]] bool IsWindowHovered() const { return m_WindowHovered; }
    [[nodiscard]] bool IsWindowFocused() const { return m_WindowFocused; }
    // Called in place of Draw for a frame the panel is left out of, as the others are while one is maximised
    void Skip();

protected:
    virtual void OnImGuiRender() = 0;

    void SetBorderless(bool borderless) { m_Borderless = borderless; }
    // One that is not leaves its keys to what it shows, as the Game panel does while the scene plays
    void SetNavigable(bool navigable) { m_Navigable = navigable; }

private:
    std::string m_Title;
    std::string m_SettingsID;
    std::string m_WindowName;
    std::string m_MenuLabel;
    DockSlot m_Slot;
    bool m_Open = true;
    bool m_Borderless = false;
    bool m_Navigable = true;
    bool m_FocusRequested = false;
    bool m_WindowHovered = false;
    bool m_WindowFocused = false;
};