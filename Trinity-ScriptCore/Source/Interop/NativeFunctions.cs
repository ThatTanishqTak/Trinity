using System.Runtime.InteropServices;

namespace Trinity.Interop
{
    // The engine's functions, which it hands to Host.Initialize as .NET starts. Laid out field for field as the engine's DotNetHost::NativeFunctions, which Initialize checks by its size
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct NativeFunctions
    {
        // The value plus one, which Host.Handshake calls so the engine can check a call into C# that calls back into the engine
        public delegate* unmanaged<int, int> Echo;
    }
}