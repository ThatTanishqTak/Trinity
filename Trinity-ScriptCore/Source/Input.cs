using System.Text;
using Trinity.Interop;

namespace Trinity
{
    // The keyboard, mouse and gamepads, as the engine's Input has them. Is is whether a key or button is held now. Was is whether it went down, or up, during this frame, so a press and a release between two frames count as both, and a key held long enough to repeat is pressed once. OnFixedUpdate may run more than once in a frame, or not at all, so a script reads Was in OnUpdate. Main thread only
    public static unsafe class Input
    {
        public static bool IsKeyPressed(Key key) => Native.Input_IsKeyPressed((int)key);
        public static bool WasKeyPressed(Key key) => Native.Input_WasKeyPressed((int)key);
        public static bool WasKeyReleased(Key key) => Native.Input_WasKeyReleased((int)key);
        public static bool IsMouseButtonPressed(MouseButton button) => Native.Input_IsMouseButtonPressed((int)button);
        public static bool WasMouseButtonPressed(MouseButton button) => Native.Input_WasMouseButtonPressed((int)button);
        public static bool WasMouseButtonReleased(MouseButton button) => Native.Input_WasMouseButtonReleased((int)button);

        // In the window's pixels, from its top left
        public static Vector2 MousePosition
        {
            get
            {
                Vector2 l_Position;
                Native.Input_GetMousePosition(&l_Position);

                return l_Position;
            }
        }

        // How far the mouse moved during this frame, in the window's pixels
        public static Vector2 MouseDelta
        {
            get
            {
                Vector2 l_Delta;
                Native.Input_GetMouseDelta(&l_Delta);

                return l_Delta;
            }
        }

        // How far the wheel turned during this frame, X sideways and Y away from the user
        public static Vector2 MouseScroll
        {
            get
            {
                Vector2 l_Scroll;
                Native.Input_GetMouseScroll(&l_Scroll);

                return l_Scroll;
            }
        }

        // Gamepads by slot, from 0 to MaxGamepads - 1. A pad keeps its slot while it is connected, and gets the same one back when it reconnects, where the platform can tell it is the same pad. Values are raw, with no deadzone, as GamepadAxis gives them. A slot with no pad, and every pad while no window of the game has the focus, reads as released and centred
        public static int MaxGamepads => Native.Input_GetMaxGamepads();

        public static bool IsGamepadConnected(int gamepad) => Native.Input_IsGamepadConnected(gamepad);
        public static bool IsGamepadButtonPressed(int gamepad, GamepadButton button) => Native.Input_IsGamepadButtonPressed(gamepad, (int)button);
        public static bool WasGamepadButtonPressed(int gamepad, GamepadButton button) => Native.Input_WasGamepadButtonPressed(gamepad, (int)button);
        public static bool WasGamepadButtonReleased(int gamepad, GamepadButton button) => Native.Input_WasGamepadButtonReleased(gamepad, (int)button);
        public static float GetGamepadAxis(int gamepad, GamepadAxis axis) => Native.Input_GetGamepadAxis(gamepad, (int)axis);

        // As the pad names itself, and empty for a slot with no pad. A new string each time, so a script reads it as a pad connects, not every frame
        public static string GetGamepadName(int gamepad)
        {
            Utf8 l_Name = Native.Input_GetGamepadName(gamepad);

            return l_Name.Length > 0 ? Encoding.UTF8.GetString(l_Name.Data, l_Name.Length) : string.Empty;
        }

        // Each motor from 0 to 1, the heavy low-frequency one in the left grip and the light high-frequency one in the right, kept until changed, the pad disconnects or play stops. Rumble stops while no window of the game has the focus, and starts again when one does
        public static void SetGamepadRumble(int gamepad, float lowFrequency, float highFrequency) => Native.Input_SetGamepadRumble(gamepad, lowFrequency, highFrequency);
        public static void StopGamepadRumble() => Native.Input_StopGamepadRumble();
    }
}