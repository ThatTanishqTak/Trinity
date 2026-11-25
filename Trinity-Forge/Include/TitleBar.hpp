#pragma once

#include <Trinity.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

// Forge's own title bar, on a window that gives the system's up: the main menu bar made as tall as one, with Trinity's mark before the menus, what is open named in the middle, and the window's three buttons at its end. The window is dragged by the bar wherever it holds nothing to click, and a double-click there maximises it or puts it back. Where the window keeps the system's, it is the menu bar and nothing more
class TitleBar
{
public:
    // Once a frame, between frames. Takes the system's title bar away or gives it back as wanted, and does what a button pressed in the last frame asked of the window
    void Update(Trinity::Window& window, bool wanted);
    [[nodiscard]] bool IsActive() const { return m_Active; }

    // In place of BeginMainMenuBar, and as it, false leaves nothing to end. The menus follow
    [[nodiscard]] bool Begin();
    // In place of EndMainMenuBar, after the last menu. requestClose is what the close button does
    void End(Trinity::Window& window, std::string_view title, const std::function<void()>& requestClose);

private:
    enum class Action : std::uint8_t
    {
        None,
        Minimize,
        ToggleMaximized,
        Drag
    };

    // What the preference last asked for, none before the first frame
    std::optional<bool> m_Wanted;
    bool m_Active = false;
    // Where the first menu begins, which the mark is before
    float m_MenusLeft = 0.0f;
    // Left for Update, as the window changes its size at once and a frame is being drawn
    Action m_Pending = Action::None;
    // The left button went down on the bar where it holds nothing to click, and is still down, so a move of the mouse drags the window
    bool m_Pressed = false;
};