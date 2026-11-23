using Trinity.Interop;

namespace Trinity
{
    // The game's UI as scripts reach it. World documents take the mouse, or a pointer a script drives
    public static unsafe class UI
    {
        // Called every frame it points. A frame without it gives the world documents back to the mouse
        public static void SetWorldPointer(Vector3 origin, Vector3 direction, bool pressed) => Native.UI_SetWorldPointer(&origin, &direction, pressed);
    }
}