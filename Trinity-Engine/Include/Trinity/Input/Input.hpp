#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Input/KeyCodes.hpp"
#include "Trinity/Input/MouseCodes.hpp"

#include <glm/glm.hpp>

namespace Trinity
{
    class Event;

    struct MousePosition
    {
        float X = 0.0f;
        float Y = 0.0f;
    };

    // The keyboard and mouse as the window's events left them. Is is whether a key or button is held now. Was is whether it went down, or up, during this frame, counted from the end of the last one, so a press and a release between two frames count as both, and a key held long enough to repeat is pressed once. OnFixedUpdate may run more than once in a frame, or not at all, so a script reads Was in OnUpdate. Main thread only
    class TRINITY_API Input
    {
    public:
        [[nodiscard]] static bool IsKeyPressed(KeyCode key);
        [[nodiscard]] static bool WasKeyPressed(KeyCode key);
        [[nodiscard]] static bool WasKeyReleased(KeyCode key);
        [[nodiscard]] static bool IsMouseButtonPressed(MouseCode button);
        [[nodiscard]] static bool WasMouseButtonPressed(MouseCode button);
        [[nodiscard]] static bool WasMouseButtonReleased(MouseCode button);
        // In the window's pixels, from its top left
        [[nodiscard]] static MousePosition GetMousePosition();
        // How far the mouse moved during this frame, in the window's pixels, 0 until it has moved once
        [[nodiscard]] static glm::vec2 GetMouseDelta();
        // How far the wheel turned during this frame, X sideways and Y away from the user, in the steps the system counts
        [[nodiscard]] static glm::vec2 GetMouseScroll();

    private:
        friend class Application;

        static void OnEvent(const Event& event);
        // After each frame's layers, so the next frame's presses, releases, movement and scrolling count from there
        static void EndFrame();
    };
}