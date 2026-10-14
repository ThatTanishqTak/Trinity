using System.Runtime.InteropServices;

namespace Trinity.Interop
{
    // Text as UTF-8 for one call into the engine, which need not end in a zero. Laid out as the engine's Interop::Utf8, which it checks at start
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct Utf8
    {
        public byte* Data;
        public int Length;

        public Utf8(byte* data, int length)
        {
            Data = data;
            Length = length;
        }
    }
}