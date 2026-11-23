// Calls between C# and the engine pass blittable types as they are, and a bool is one byte
[assembly: System.Runtime.CompilerServices.DisableRuntimeMarshalling]