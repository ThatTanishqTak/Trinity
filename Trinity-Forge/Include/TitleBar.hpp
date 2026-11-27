#pragma once

#include <Trinity.hpp>
#include <Trinity/Tools/WindowChrome.hpp>

#include <functional>
#include <string_view>

// Forge's own title bar, on a window that gives the system's up: the main menu bar made as tall as one, with Trinity's mark before the menus, what is open named in the middle, and the window's three buttons at its end. The window is dragged by the bar wherever it holds nothing to click, and a double-click there maximises it or puts it back. Where the window keeps the system's, it is the menu bar and nothing more
class TitleBar
{
public:
    // Once a frame, between frames. Takes the system's title bar away or gives it back as wanted, and does what a button pressed in the last frame asked of the window
    void Update(Trinity::Window& window, bool wanted) { m_Chrome.Update(window, wanted); }
    [[nodiscard]] bool IsActive() const { return m_Chrome.IsActive(); }

    // In place of BeginMainMenuBar, and as it, false leaves nothing to end. The menus follow
    [[nodiscard]] bool Begin();
    // In place of EndMainMenuBar, after the last menu. requestClose is what the close button does
    void End(Trinity::Window& window, std::string_view title, const std::function<void()>& requestClose);

private:
    // The buttons, the mark and the dragging, which Hub's title bar has too
    Trinity::Tools::WindowChrome m_Chrome;
    // Where the first menu begins, which the mark is before
    float m_MenusLeft = 0.0f;
};