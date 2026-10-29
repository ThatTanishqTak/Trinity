using Trinity.Interop;

namespace Trinity
{
    // How the game's window shows it, as Unity's FullScreenMode names them and the engine numbers them: framed at its own size, or without a frame over the whole of its monitor, at the monitor's own size
    public enum FullScreenMode
    {
        Windowed,
        FullScreenWindow
    }

    // The game's screen, as Unity's Screen has it: the size the game draws at, its mode and vsync. A change applies once the frame ends, so reads give the screen as it is until then. A player keeps the change as its player's choice, and Forge's Game panel ignores it with one line. In FullScreenWindow the game draws at the monitor's size, and SetResolution's size is the window's when it goes back
    public static class Screen
    {
        public static int Width => Native.Screen_GetWidth();
        public static int Height => Native.Screen_GetHeight();

        public static FullScreenMode FullScreenMode
        {
            get => (FullScreenMode)Native.Screen_GetFullScreenMode();
            set => Native.Screen_SetFullScreenMode((int)value);
        }

        public static bool FullScreen
        {
            get => FullScreenMode != FullScreenMode.Windowed;
            set => FullScreenMode = value ? FullScreenMode.FullScreenWindow : FullScreenMode.Windowed;
        }

        public static bool VSync
        {
            get => Native.Screen_IsVSync();
            set => Native.Screen_SetVSync(value);
        }

        public static void SetResolution(int width, int height, FullScreenMode mode) => Native.Screen_SetResolution(width, height, (int)mode);

        public static void SetResolution(int width, int height, bool fullScreen) => SetResolution(width, height, fullScreen ? FullScreenMode.FullScreenWindow : FullScreenMode.Windowed);
    }
}