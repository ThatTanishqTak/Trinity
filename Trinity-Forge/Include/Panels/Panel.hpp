#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Where a panel goes in the default layout
enum class DockSlot : std::uint8_t
{
    Centre,
    Left,
    Right,
    Bottom
};

// A dockable Forge window. ImGui knows it by its ID alone, its title unless it is given another, so the icon and the title can change without losing its place in a saved layout
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
    // Brings the panel to the front the next time it is drawn, selecting its tab where it is docked with others
    void RequestFocus() { m_FocusRequested = true; }

protected:
    virtual void OnImGuiRender() = 0;

    void SetBorderless(bool borderless) { m_Borderless = borderless; }

private:
    std::string m_Title;
    std::string m_SettingsID;
    std::string m_WindowName;
    std::string m_MenuLabel;
    DockSlot m_Slot;
    bool m_Open = true;
    bool m_Borderless = false;
    bool m_FocusRequested = false;
};