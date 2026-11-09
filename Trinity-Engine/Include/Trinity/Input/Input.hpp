#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Input/GamepadCodes.hpp"
#include "Trinity/Input/KeyCodes.hpp"
#include "Trinity/Input/MouseCodes.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <span>
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

    // A key going down, repeating as it is held, or going up, as the window told it
    struct KeyStroke
    {
        KeyCode Key = KeyCode::UNKNOWN;
        bool Down = false;
        bool Repeat = false;
    };

    // The keyboard and mouse as the window's events left them, and the gamepads as the platform last read them. Is is whether a key or button is held now. Was is whether it went down, or up, during this frame, counted from the end of the last one, so a press and a release between two frames count as both, and a key held long enough to repeat is pressed once. OnFixedUpdate may run more than once in a frame, or not at all, so a script reads Was in OnUpdate. While a scene's frame runs, every read, its scripts' own and its actions', is of what SetSceneFilter lets the scene have, less what the game UI took of it, and the rest are of the window, so an editor reads the devices whatever its scene is let have. The game UI reads before the scene, while SetReadingUI is on, what the filter lets the scene have before anything is taken. Main thread only
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
        // Whether this machine has what pads are read through, the GameInput runtime on Windows, without which no pad is read, as Hub's Installs page says. True where there is nothing to install
        [[nodiscard]] static bool HasGamepadRuntime();

        // Taken as the frame ends, so the scene's next frame reads by it, and kept until it is set again
        static void SetSceneFilter(const SceneInputFilter& filter);

        // The game UI's reads, made as the scene's frame begins and before its actions read: while on, every read is of what the filter lets the scene have, as nothing had been taken. Returns whether it was on, to go back to
        static bool SetReadingUI(bool reading);
        // This frame's keys going down, repeating and up, in order, and the text typed, as the filter let the scene have the keyboard, for the game UI to edit text by. A frame keeps at most c_MaxKeyStrokes and c_MaxTypedCharacters
        [[nodiscard]] static std::span<const KeyStroke> GetKeyStrokes();
        [[nodiscard]] static std::u32string_view GetTypedText();
        // What the game UI used, kept from the scene. A key or button reads to it as released from now until it is pressed again, with no press this frame and a release if it saw it held. A pad's axis reads 0 to it until the stick comes back under half way, and the wheel's turn this frame reads 0
        static void TakeKey(KeyCode key);
        static void TakeMouseButton(MouseCode button);
        static void TakeMouseScroll();
        static void TakeGamepadButton(std::uint32_t gamepad, GamepadButton button);
        static void TakeGamepadAxis(std::uint32_t gamepad, GamepadAxis axis);

        static constexpr std::size_t c_MaxKeyStrokes = 64;
        static constexpr std::size_t c_MaxTypedCharacters = 64;

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