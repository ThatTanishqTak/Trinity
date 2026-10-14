using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Trinity.Interop
{
    // ScriptCore's entry points, which the engine finds by name once .NET has started and calls through function pointers. None may throw, since an exception cannot cross into native code
    public static unsafe class Host
    {
        private static NativeFunctions s_Functions;

        // 0 once the engine's functions are taken, and 1 when the table is not the size this ScriptCore was built for, which means the engine and ScriptCore come from different builds
        [UnmanagedCallersOnly]
        public static int Initialize(NativeFunctions* functions, int size)
        {
            if (functions == null || size != sizeof(NativeFunctions))
            {
                return 1;
            }

            s_Functions = *functions;

            return 0;
        }

        [UnmanagedCallersOnly]
        public static void Shutdown()
        {
            s_Functions = default;
        }

        // The runtime's description, such as .NET 10.0.0, written as UTF-8 into the buffer when it fits. Its length in bytes either way
        [UnmanagedCallersOnly]
        public static int GetRuntimeDescription(byte* buffer, int capacity)
        {
            string l_Description = RuntimeInformation.FrameworkDescription;
            int l_Length = Encoding.UTF8.GetByteCount(l_Description);
            if (buffer != null && l_Length <= capacity)
            {
                Encoding.UTF8.GetBytes(l_Description, new Span<byte>(buffer, capacity));
            }

            return l_Length;
        }

        // A call into C# that calls back into the engine: the engine's Echo of the value, doubled
        [UnmanagedCallersOnly]
        public static int Handshake(int value)
        {
            return s_Functions.Echo != null ? s_Functions.Echo(value) * 2 : 0;
        }
    }
}