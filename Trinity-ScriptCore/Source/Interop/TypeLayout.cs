using System.Runtime.InteropServices;

namespace Trinity.Interop
{
    // A shared type's size and its fields' offsets, laid out as the engine's Interop::TypeLayout, which Host.DescribeLayouts fills for the engine to compare with its own
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct TypeLayout
    {
        public const int MaxFields = 16;

        public int Size;
        public int FieldCount;
        public fixed int Offsets[MaxFields];
    }
}