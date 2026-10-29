using Trinity.Interop;

namespace Trinity
{
    // The game's end. Quit closes a player once the frame ends, as Unity's Application.Quit does, and stops playing in Forge
    public static class Application
    {
        public static void Quit() => Native.Application_Quit();
    }
}