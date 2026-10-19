using System;
using Trinity.Interop;

namespace Trinity
{
    // What a rebind came to, numbered as the engine's InputRebindState numbers it. Waiting while it listens, Bound once made and saved, Refused when another binding of the action's map already reads the source for the player, and Canceled by Escape, Cancel, another rebind starting to listen, or its action leaving the asset. None for no rebind, and for one made more than 8 rebinds ago or before the scene last stopped
    public enum InputRebindState
    {
        None,
        Waiting,
        Bound,
        Refused,
        Canceled
    }

    // A rebind an action made, as the engine's InputRebind is, read until 8 more have been made, as a controls menu waits on it. Main thread only
    public readonly struct InputRebind : IEquatable<InputRebind>
    {
        internal InputRebind(ulong handle)
        {
            Handle = handle;
        }

        public static InputRebind None => default;

        // The engine's handle, 0 for None
        internal ulong Handle { get; }

        public InputRebindState State => (InputRebindState)Native.InputRebind_GetState(Handle);
        public bool IsDone => State != InputRebindState.Waiting;

        // The source it took, or the one it refused, by its name, such as Keyboard/J, and empty while it waits. A new string each time
        public string Source => Native.InputRebind_GetSource(Handle).ToString();

        // When it was refused, the name of the action of the same map that reads the source, and empty otherwise. A new string each time
        public string Conflict => Native.InputRebind_GetConflict(Handle).ToString();

        // Stops one that waits, which then reads as Canceled
        public void Cancel() => Native.InputRebind_Cancel(Handle);

        public bool Equals(InputRebind other) => Handle == other.Handle;
        public override bool Equals(object? obj) => obj is InputRebind other && Equals(other);
        public override int GetHashCode() => Handle.GetHashCode();
        public override string ToString() => $"{State}{(Source.Length != 0 ? $" {Source}" : string.Empty)}";

        public static bool operator ==(InputRebind left, InputRebind right) => left.Equals(right);
        public static bool operator !=(InputRebind left, InputRebind right) => !left.Equals(right);
    }
}