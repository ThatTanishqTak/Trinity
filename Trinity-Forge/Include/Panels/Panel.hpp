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

// A dockable Forge window. ImGui knows it by its title alone, so the icon can change without losing its place in a saved layout
class Panel
{
public:
    Panel(std::string_view title, std::string_view icon, DockSlot slot);
    virtual ~Panel() = default;

    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    void Draw();

    [[nodiscard]] const std::string& GetTitle() const { return m_Title; }
    [[nodiscard]] const std::string& GetWindowName() const { return m_WindowName; }
    [[nodiscard]] const std::string& GetMenuLabel() const { return m_MenuLabel; }
    [[nodiscard]] DockSlot GetSlot() const { return m_Slot; }

    [[nodiscard]] bool IsOpen() const { return m_Open; }
    void SetOpen(bool open);

protected:
    virtual void OnImGuiRender() = 0;

private:
    std::string m_Title;
    std::string m_WindowName;
    std::string m_MenuLabel;
    DockSlot m_Slot;
    bool m_Open = true;
};