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

    // What a scene shown in a panel of an editor is let read, as Forge's Game panel sets it. A device kept from the scene reads to it as released, centred and still, with a release for whatever it held, and a key, button or axis held as the scene gets it back stays hidden from it until it is let go, so the click that focuses the panel is not the game's. Mouse lets the movement in and keeps the buttons held, while a new press or the wheel also needs MouseOver, the pointer over the scene's image. The scene reads the mouse in the image's pixels, from MouseOrigin in the window's pixels, MouseScale of the window's pixels to one of its own. One never set lets the scene read everything as the window has it
    struct SceneInputFilter
    {
        bool Keyboard = true;
        bool Mouse = true;
        bool MouseOver = true;
        bool Gamepads = true;
        glm::vec2 MouseOrigin{ 0.0f };
        float MouseScale = 1.0f;
    };

    // The keyboard and mouse as the window's events left them, and the gamepads as the platform last read them. Is is whether a key or button is held now. Was is whether it went down, or up, during this frame, counted from the end of the last one, so a press and a release between two frames count as both, and a key held long enough to repeat is pressed once. OnFixedUpdate may run more than once in a frame, or not at all, so a script reads Was in OnUpdate. While a scene's frame runs, every read, its scripts' own and its actions', is of what SetSceneFilter lets the scene have, and the rest are of the window, so an editor reads the devices whatever its scene is let have. Main thread only
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
        // Every pad's, as when a SceneManager ends, so stopping play in Forge leaves no pad rumbling
        static void StopGamepadRumble();

        // Taken as the frame ends, so the scene's next frame reads by it, and kept until it is set again
        static void SetSceneFilter(const SceneInputFilter& filter);

    private:
        friend class Application;
        friend class SceneRuntime;

        static void OnEvent(const Event& event);
        // After the window's events each frame. Reports pads connecting and disconnecting through onEvent, with Input already reading them that way
        static void PollGamepads(const std::function<void(Event&)>& onEvent);
        // After each frame's layers, so the next frame's presses, releases, movement and scrolling count from there
        static void EndFrame();
        static void InitializeGamepads();
        static void ShutdownGamepads();
        // Whether reads are the scene's, as its frame runs, returning whether they were, to go back to
        static bool SetReadingScene(bool reading);
    };
}