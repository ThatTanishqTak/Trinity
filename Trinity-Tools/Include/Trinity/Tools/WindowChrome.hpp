#pragma once

#include <Trinity.hpp>

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace Trinity::Tools
{
    // What Forge's and Hub's own title bars share, on a window that gives the system's up: the window's three buttons, Trinity's mark, and the window dragged by the bar where it holds nothing to click, a double-click there maximising it or putting it back
    class WindowChrome
    {
    public:
        // Once a frame, between frames. Takes the system's title bar away or gives it back as wanted, and does what the last frame's buttons and drags asked of the window
        void Update(Window& window, bool wanted);
        // False where the window keeps the system's title bar, which then does all of this itself
        [[nodiscard]] bool IsActive() const { return m_Active; }

        // The padding above and below a title bar's text, and how tall that makes the bar, beside text of the size in use: as tall as Windows' own
        [[nodiscard]] static float GetPadding();
        [[nodiscard]] static float GetHeight();
        // The room the window's three buttons take at a bar's end
        [[nodiscard]] static float GetButtonsWidth();

        // Trinity's mark: three sides, in the accent, about the point given
        static void DrawMark(ImDrawList& drawList, const ImVec2& centre, float side);
        // Minimise, maximise or restore, and close, side by side from minimum to maximum, in the window being drawn. requestClose is what the close button does
        void DrawButtons(const Window& window, const ImVec2& minimum, const ImVec2& maximum, const std::function<void()>& requestClose);
        // Once a frame, with whether the mouse is over a part of the bar that holds nothing to click. A double-click there maximises the window or puts it back, and a press there that then moves hands the window to the system to drag
        void DragBy(bool empty);

    private:
        enum class Action : std::uint8_t
        {
            None,
            Minimize,
            ToggleMaximized,
            Drag
        };

        // What was last asked for, none before the first frame
        std::optional<bool> m_Wanted;
        bool m_Active = false;
        // Left for Update, as the window changes its size at once and a frame is being drawn
        Action m_Pending = Action::None;
        // The left button went down on an empty part of the bar and is still down, so a move of the mouse drags the window
        bool m_Pressed = false;
    };
}