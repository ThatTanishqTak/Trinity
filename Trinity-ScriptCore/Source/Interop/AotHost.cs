using System.Runtime.InteropServices;
using System.Text;

namespace Trinity.Interop
{
    // The way into a game's NativeAOT library, which holds ScriptCore and the game's scripts compiled ahead of time, so a player needs no .NET installed. No hostfxr is there to find ScriptCore's entry points by name, so this export finds them by the same type and method names, and the engine calls them as it calls them through hostfxr. Only such a library exports it, as Trinity-ScriptCore.Aot.targets asks
    public static unsafe class AotHost
    {
        private const string c_HostType = "Trinity.Interop.Host, Trinity-ScriptCore";
        private const string c_ScriptHostType = "Trinity.Interop.ScriptHost, Trinity-ScriptCore";

        // The names in UTF-8. 0 for an entry point there is not, which the engine reports as it reports one hostfxr does not find
        [UnmanagedCallersOnly(EntryPoint = "TrinityScriptCore_GetEntryPoint")]
        public static nint GetEntryPoint(byte* type, int typeLength, byte* method, int methodLength)
        {
            return (Encoding.UTF8.GetString(type, typeLength), Encoding.UTF8.GetString(method, methodLength)) switch
            {
                (c_HostType, "Initialize") => (nint)(delegate* unmanaged<NativeFunctions*, int, int>)&Host.Initialize,
                (c_HostType, "Shutdown") => (nint)(delegate* unmanaged<void>)&Host.Shutdown,
                (c_HostType, "GetRuntimeDescription") => (nint)(delegate* unmanaged<byte*, int, int>)&Host.GetRuntimeDescription,
                (c_HostType, "Handshake") => (nint)(delegate* unmanaged<int, int>)&Host.Handshake,
                (c_HostType, "DescribeLayouts") => (nint)(delegate* unmanaged<TypeLayout*, int, int, int>)&Host.DescribeLayouts,
                (c_HostType, "MeasureInterop") => (nint)(delegate* unmanaged<int, long>)&Host.MeasureInterop,
                (c_HostType, "IsDebuggerAttached") => (nint)(delegate* unmanaged<int>)&Host.IsDebuggerAttached,
                (c_ScriptHostType, "LoadAssembly") => (nint)(delegate* unmanaged<byte*, int, byte*, int, int>)&ScriptHost.LoadAssembly,
                (c_ScriptHostType, "LoadCompiled") => (nint)(delegate* unmanaged<byte*, int, int>)&ScriptHost.LoadCompiled,
                (c_ScriptHostType, "UnloadAssembly") => (nint)(delegate* unmanaged<int>)&ScriptHost.UnloadAssembly,
                (c_ScriptHostType, "GetHeapStatistics") => (nint)(delegate* unmanaged<long*, int*, void>)&ScriptHost.GetHeapStatistics,
                (c_ScriptHostType, "CreateInstance") => (nint)(delegate* unmanaged<int, ulong, nint>)&ScriptHost.CreateInstance,
                (c_ScriptHostType, "DestroyInstance") => (nint)(delegate* unmanaged<nint, void>)&ScriptHost.DestroyInstance,
                (c_ScriptHostType, "SetField") => (nint)(delegate* unmanaged<nint, int, FieldValue*, int>)&ScriptHost.SetField,
                (c_ScriptHostType, "GetField") => (nint)(delegate* unmanaged<nint, int, FieldValue*, int>)&ScriptHost.GetField,
                (c_ScriptHostType, "SetElement") => (nint)(delegate* unmanaged<nint, int, int, FieldValue*, int>)&ScriptHost.SetElement,
                (c_ScriptHostType, "GetElement") => (nint)(delegate* unmanaged<nint, int, int, FieldValue*, int>)&ScriptHost.GetElement,
                (c_ScriptHostType, "Invoke") => (nint)(delegate* unmanaged<nint, int, float, int>)&ScriptHost.Invoke,
                (c_ScriptHostType, "InvokePhysics") => (nint)(delegate* unmanaged<nint, int, Collision*, int>)&ScriptHost.InvokePhysics,
                (c_ScriptHostType, "ResumeTask") => (nint)(delegate* unmanaged<nint, int*, float*, int*, int>)&ScriptHost.ResumeTask,
                (c_ScriptHostType, "CheckTask") => (nint)(delegate* unmanaged<nint, int*, int>)&ScriptHost.CheckTask,
                (c_ScriptHostType, "FreeTask") => (nint)(delegate* unmanaged<nint, void>)&ScriptHost.FreeTask,
                (c_ScriptHostType, "RaiseInputEvents") => (nint)(delegate* unmanaged<void>)&ScriptHost.RaiseInputEvents,
                _ => 0
            };
        }
    }
}