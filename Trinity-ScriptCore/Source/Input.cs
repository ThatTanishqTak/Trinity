using System.Text;
using Trinity.Interop;

namespace Trinity
{
    // Is reads what is held, Was this frame's presses, so a script reads Was in OnUpdate. Main thread
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

        // A pad keeps its slot while connected. An empty slot, and any pad without focus, reads released
        public static int MaxGamepads => Native.Input_GetMaxGamepads();

        public static bool IsGamepadConnected(int gamepad) => Native.Input_IsGamepadConnected(gamepad);
        public static bool IsGamepadButtonPressed(int gamepad, GamepadButton button) => Native.Input_IsGamepadButtonPressed(gamepad, (int)button);
        public static bool WasGamepadButtonPressed(int gamepad, GamepadButton button) => Native.Input_WasGamepadButtonPressed(gamepad, (int)button);
        public static bool WasGamepadButtonReleased(int gamepad, GamepadButton button) => Native.Input_WasGamepadButtonReleased(gamepad, (int)button);
        public static float GetGamepadAxis(int gamepad, GamepadAxis axis) => Native.Input_GetGamepadAxis(gamepad, (int)axis);

        // As the pad names itself, empty for an empty slot. A new string each time, so not for every frame
        public static string GetGamepadName(int gamepad)
        {
            Utf8 l_Name = Native.Input_GetGamepadName(gamepad);

            return l_Name.Length > 0 ? Encoding.UTF8.GetString(l_Name.Data, l_Name.Length) : string.Empty;
        }

        // Each motor from 0 to 1, kept until changed or play stops. Stops while no window has the focus
        public static void SetGamepadRumble(int gamepad, float lowFrequency, float highFrequency) => Native.Input_SetGamepadRumble(gamepad, lowFrequency, highFrequency);
        public static void StopGamepadRumble() => Native.Input_StopGamepadRumble();
    }
}