using Trinity.Interop;

namespace Trinity
{
    // As Unity's FullScreenMode, numbered as the engine's: framed at its size, or over the monitor
    public enum FullScreenMode
    {
        Windowed,
        FullScreenWindow
    }

    // As Unity's Screen. A change applies once the frame ends. Forge's Game panel ignores changes
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