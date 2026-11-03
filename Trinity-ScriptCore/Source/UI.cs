using Trinity.Interop;

namespace Trinity
{
    // The game's UI as scripts reach it. World documents, drawn in the scene, take the mouse through the game view, or a pointer a script drives in its place
    public static unsafe class UI
    {
        // In place of the mouse for the world documents, from the next frame: a ray from the origin along the direction, which need not be of unit length, meeting nothing behind the origin, and whether its button, the primary one, is held, as a crosshair or a VR laser points. Called every frame it points, as a frame without it gives the world documents back to the mouse and ends a press it held. Colliders hide documents from it as from the mouse
        public static void SetWorldPointer(Vector3 origin, Vector3 direction, bool pressed) => Native.UI_SetWorldPointer(&origin, &direction, pressed);
    }
}