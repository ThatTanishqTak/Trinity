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

    // What an editor's panel lets its scene read. Mouse presses and the wheel also need MouseOver
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

    // Was is this frame's presses and releases, so a script reads it in OnUpdate. Main thread only
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
        // How far the wheel turned this frame, X sideways and Y away from the user, in the system's steps
        [[nodiscard]] static glm::vec2 GetMouseScroll();

        // A pad keeps its slot while connected. An empty slot, and any pad without focus, reads released
        [[nodiscard]] static bool IsGamepadConnected(std::uint32_t gamepad);
        // As the pad names itself, and empty for a slot with no pad
        [[nodiscard]] static std::string_view GetGamepadName(std::uint32_t gamepad);
        [[nodiscard]] static bool IsGamepadButtonPressed(std::uint32_t gamepad, GamepadButton button);
        [[nodiscard]] static bool WasGamepadButtonPressed(std::uint32_t gamepad, GamepadButton button);
        [[nodiscard]] static bool WasGamepadButtonReleased(std::uint32_t gamepad, GamepadButton button);
        [[nodiscard]] static float GetGamepadAxis(std::uint32_t gamepad, GamepadAxis axis);
        // Each from 0 to 1, kept until changed or the pad disconnects. Stops while no window has the focus
        static void SetGamepadRumble(std::uint32_t gamepad, float lowFrequency, float highFrequency);
        // Every pad's, as when a SceneManager ends, so stopping play in Forge leaves no pad rumbling
        static void StopGamepadRumble();
        // False only on Windows without the GameInput runtime, where no pad is read
        [[nodiscard]] static bool HasGamepadRuntime();

        // Taken as the frame ends, so the scene's next frame reads by it, and kept until it is set again
        static void SetSceneFilter(const SceneInputFilter& filter);

        // While on, reads are what the filter lets the scene have, nothing taken. Returns the value before
        static bool SetReadingUI(bool reading);
        // This frame's key strokes in order, as the filter let them in. At most c_MaxKeyStrokes a frame
        [[nodiscard]] static std::span<const KeyStroke> GetKeyStrokes();
        [[nodiscard]] static std::u32string_view GetTypedText();
        // Kept from the scene: it reads as released until pressed again, and an axis as 0 until under half
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
        // After the window's events each frame. Reports pads connecting and disconnecting through onEvent
        static void PollGamepads(const std::function<void(Event&)>& onEvent);
        // After each frame's layers, so the next frame's presses, releases and movement count from there
        static void EndFrame();
        static void InitializeGamepads();
        static void ShutdownGamepads();
        // Whether reads are the scene's, as its frame runs, returning whether they were, to go back to
        static bool SetReadingScene(bool reading);
    };
}