using System.Runtime.InteropServices;

namespace Trinity.Interop
{
    // Laid out as the engine's Interop::TypeLayout, which Host.DescribeLayouts fills
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct TypeLayout
    {
        public const int MaxFields = 16;

        public int Size;
        public int FieldCount;
        public fixed int Offsets[MaxFields];
    }
}