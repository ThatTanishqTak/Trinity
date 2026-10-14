using System;
using System.Buffers;
using System.Text;

namespace Trinity.Interop
{
    // Text as UTF-8 for one call into the engine: on the caller's stack, or in a rented buffer when it is long, so passing it allocates nothing it keeps. Disposed once the call has returned
    internal ref struct Utf8String
    {
        public const int c_StackBytes = 256;

        private byte[]? m_Rented;

        public Utf8String(string? text, Span<byte> stack)
        {
            text ??= string.Empty;
            int l_MaxBytes = Encoding.UTF8.GetMaxByteCount(text.Length);
            Span<byte> l_Buffer = l_MaxBytes <= stack.Length ? stack : (m_Rented = ArrayPool<byte>.Shared.Rent(l_MaxBytes));
            Bytes = l_Buffer.Slice(0, Encoding.UTF8.GetBytes(text, l_Buffer));
        }

        public Span<byte> Bytes { get; }

        public void Dispose()
        {
            if (m_Rented != null)
            {
                ArrayPool<byte>.Shared.Return(m_Rented);
                m_Rented = null;
            }
        }
    }
}