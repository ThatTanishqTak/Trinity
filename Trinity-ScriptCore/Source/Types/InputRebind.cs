using System;
using Trinity.Interop;

namespace Trinity
{
    // Numbered as the engine's InputRebindState. None for no rebind, or one more than 8 rebinds old
    public enum InputRebindState
    {
        None,
        Waiting,
        Bound,
        Refused,
        Canceled
    }

    // Read until 8 more have been made, as a controls menu waits on it. Main thread only
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

        // The source it took or refused, by name, such as Keyboard/J. Empty while it waits. A new string
        public string Source => Native.InputRebind_GetSource(Handle).ToString();

        // When refused, the action of the same map that reads the source. Empty otherwise. A new string
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