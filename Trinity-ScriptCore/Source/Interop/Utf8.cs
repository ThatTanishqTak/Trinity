using System.Runtime.InteropServices;
using System.Text;

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

        // A copy, as text the engine gives back is valid only until its next call that gives text. Empty for none
        public override string ToString() => Length > 0 ? Encoding.UTF8.GetString(Data, Length) : string.Empty;
    }
}