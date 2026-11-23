using Trinity.Interop;

namespace Trinity
{
    // Quit closes a player once the frame ends, as Unity's Application.Quit, and stops play in Forge
    public static class Application
    {
        public static void Quit() => Native.Application_Quit();
    }
}