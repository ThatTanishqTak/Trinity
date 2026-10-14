using Trinity.Interop;

namespace Trinity
{
    // The keyboard and mouse, as the engine's Input has them. Is is whether a key or button is held now. Was is whether it went down, or up, during this frame, so a press and a release between two frames count as both, and a key held long enough to repeat is pressed once. OnFixedUpdate may run more than once in a frame, or not at all, so a script reads Was in OnUpdate. Main thread only
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
    }
}