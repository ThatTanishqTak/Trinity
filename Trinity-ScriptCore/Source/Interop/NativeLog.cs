using System;
using System.Buffers;
using System.Text;

namespace Trinity.Interop
{
    // Text to the engine's log, encoded as UTF-8 on the stack, or in a rented buffer when it is long, so writing it allocates nothing it keeps
    internal static unsafe class NativeLog
    {
        private const int c_StackBytes = 512;

        public static void Write(LogLevel level, string text)
        {
            int l_MaxBytes = Encoding.UTF8.GetMaxByteCount(text.Length);
            byte[]? l_Rented = l_MaxBytes > c_StackBytes ? ArrayPool<byte>.Shared.Rent(l_MaxBytes) : null;
            Span<byte> l_Buffer = l_Rented != null ? l_Rented : stackalloc byte[c_StackBytes];
            int l_Length = Encoding.UTF8.GetBytes(text, l_Buffer);
            fixed (byte* l_Data = l_Buffer)
            {
                Native.Log_Write(level, new Utf8(l_Data, l_Length));
            }

            if (l_Rented != null)
            {
                ArrayPool<byte>.Shared.Return(l_Rented);
            }
        }
    }
}