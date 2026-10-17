#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Input/GamepadCodes.hpp"
#include "Trinity/Input/KeyCodes.hpp"
#include "Trinity/Input/MouseCodes.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string_view>

namespace Trinity
{
    class Event;

    struct MousePosition
    {
        float X = 0.0f;
        float Y = 0.0f;
    };

    // The keyboard and mouse as the window's events left them, and the gamepads as the platform last read them. Is is whether a key or button is held now. Was is whether it went down, or up, during this frame, counted from the end of the last one, so a press and a release between two frames count as both, and a key held long enough to repeat is pressed once. OnFixedUpdate may run more than once in a frame, or not at all, so a script reads Was in OnUpdate. Main thread only
    class TRINITY_API Input
    {
    public:
        static constexpr std::uint32_t c_MaxGamepads = 8;

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

        // Gamepads by slot, from 0 to c_MaxGamepads - 1. A pad keeps its slot while it is connected, and gets the same one back when it reconnects, where the platform can tell it is the same pad. Values are raw, as GamepadAxis gives them. A slot with no pad, and every pad while no window of the application has the focus, reads as released and centred
        [[nodiscard]] static bool IsGamepadConnected(std::uint32_t gamepad);
        // As the pad names itself, and empty for a slot with no pad
        [[nodiscard]] static std::string_view GetGamepadName(std::uint32_t gamepad);
        [[nodiscard]] static bool IsGamepadButtonPressed(std::uint32_t gamepad, GamepadButton button);
        [[nodiscard]] static bool WasGamepadButtonPressed(std::uint32_t gamepad, GamepadButton button);
        [[nodiscard]] static bool WasGamepadButtonReleased(std::uint32_t gamepad, GamepadButton button);
        [[nodiscard]] static float GetGamepadAxis(std::uint32_t gamepad, GamepadAxis axis);
        // Each motor from 0 to 1, the heavy low-frequency one in the left grip and the light high-frequency one in the right, kept until changed or the pad disconnects. Rumble stops while no window of the application has the focus, and starts again when one does
        static void SetGamepadRumble(std::uint32_t gamepad, float lowFrequency, float highFrequency);

    private:
        friend class Application;

        static void OnEvent(const Event& event);
        // After the window's events each frame. Reports pads connecting and disconnecting through onEvent, with Input already reading them that way
        static void PollGamepads(const std::function<void(Event&)>& onEvent);
        // After each frame's layers, so the next frame's presses, releases, movement and scrolling count from there
        static void EndFrame();
        static void InitializeGamepads();
        static void ShutdownGamepads();
    };
}