using System.Runtime.InteropServices;

namespace Trinity.Interop
{
    // Laid out as the engine's Interop::Collision, which it checks at start
    [StructLayout(LayoutKind.Sequential)]
    public struct Collision
    {
        public ulong Other;
        public Vector3 Point;
        public Vector3 Normal;
        public bool Is2D;
    }
}