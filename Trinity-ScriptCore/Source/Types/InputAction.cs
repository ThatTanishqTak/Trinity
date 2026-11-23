using System;
using Trinity.Interop;

namespace Trinity
{
    // Found once by its Map/Action path. In OnFixedUpdate, Was is since the step before. Main thread
    public readonly struct InputAction : IEquatable<InputAction>
    {
        private readonly string? m_Path;

        public static InputAction None => default;

        private InputAction(ulong handle, string path)
        {
            Handle = handle;
            m_Path = path;
        }

        // The engine's handle, 0 for None and for one found while no scene ran
        internal ulong Handle { get; }

        // As it was found
        public string Path => m_Path ?? string.Empty;

        public static unsafe InputAction Find(string path)
        {
            ArgumentNullException.ThrowIfNull(path);
            using Utf8String l_Path = new(path, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Path.Bytes)
            {
                return new InputAction(Native.InputAction_Find(new Utf8(l_Data, l_Path.Bytes.Length)), path);
            }
        }

        // Found while a scene runs, and in its asset now
        public bool IsValid => Native.InputAction_IsValid(Handle);

        public bool IsPressed(int player = 0) => Native.InputAction_IsPressed(Handle, player);
        public bool WasPressed(int player = 0) => Native.InputAction_WasPressed(Handle, player);
        public bool WasReleased(int player = 0) => Native.InputAction_WasReleased(Handle, player);

        // Its interaction completing, and failing once begun. Without one, it performs as it is pressed
        public bool WasPerformed(int player = 0) => Native.InputAction_WasPerformed(Handle, player);
        public bool WasCanceled(int player = 0) => Native.InputAction_WasCanceled(Handle, player);

        // From 0 to 1: a Hold's time over its Duration, or a MultiTap's taps over its Count
        public float ReadProgress(int player = 0) => Native.InputAction_ReadProgress(Handle, player);

        // A Button's strength from 0 to 1, an Axis from -1 to 1, and a Vector2's length
        public float ReadValue(int player = 0) => Native.InputAction_ReadValue(Handle, player);

        // A Vector2's X and Y, and a Button's or an Axis's value as X, with Y 0
        public unsafe Vector2 ReadVector2(int player = 0)
        {
            Vector2 l_Value;
            Native.InputAction_ReadVector2(Handle, player, &l_Value);

            return l_Value;
        }

        // Each a single source, or a composite of 2 or 4 parts. 0 for an action the asset lacks
        public int BindingCount => Native.InputAction_GetBindingCount(Handle);
        public int GetPartCount(int binding) => Native.InputAction_GetPartCount(Handle, binding);

        // A part's source by name, such as Keyboard/Space. A new string each time, so not for every frame
        public string GetSource(int binding, int part = 0, int player = 0) => Native.InputAction_GetSource(Handle, binding, part, player, false).ToString();
        public string GetDefaultSource(int binding, int part = 0) => Native.InputAction_GetSource(Handle, binding, part, 0, true).ToString();

        // Saved in /saves, never in the asset. Refused if its map already reads the source for the player
        public unsafe InputRebind Rebind(int binding, int part, string source, int player = 0)
        {
            ArgumentNullException.ThrowIfNull(source);
            using Utf8String l_Source = new(source, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Source.Bytes)
            {
                return new InputRebind(Native.InputAction_Rebind(Handle, binding, part, new Utf8(l_Data, l_Source.Bytes.Length), player));
            }
        }

        // Listens from the next frame for a key, button, stick or trigger. Escape and Cancel stop it
        public InputRebind StartRebind(int binding, int part = 0, int player = 0) => new(Native.InputAction_StartRebind(Handle, binding, part, player));

        // Back to the asset's source, saved as Rebind is
        public void ResetBinding(int binding, int part = 0, int player = 0) => Native.InputAction_ResetBinding(Handle, binding, part, player);

        // Once a frame for each player it went down for. A script removes its handler in OnDestroy
        public event Action<InputActionEvent> Pressed
        {
            add => InputEvents.Add(this, InputEdge.Pressed, value);
            remove => InputEvents.Remove(this, InputEdge.Pressed, value);
        }

        // As Pressed, for each player it went up for, as WasReleased reads in OnUpdate
        public event Action<InputActionEvent> Released
        {
            add => InputEvents.Add(this, InputEdge.Released, value);
            remove => InputEvents.Remove(this, InputEdge.Released, value);
        }

        // As Pressed, for each player its interaction completed for, as WasPerformed reads in OnUpdate
        public event Action<InputActionEvent> Performed
        {
            add => InputEvents.Add(this, InputEdge.Performed, value);
            remove => InputEvents.Remove(this, InputEdge.Performed, value);
        }

        // As Pressed, for each player its interaction failed for once begun, as WasCanceled reads
        public event Action<InputActionEvent> Canceled
        {
            add => InputEvents.Add(this, InputEdge.Canceled, value);
            remove => InputEvents.Remove(this, InputEdge.Canceled, value);
        }

        public bool Equals(InputAction other) => Handle == other.Handle;
        public override bool Equals(object? obj) => obj is InputAction other && Equals(other);
        public override int GetHashCode() => Handle.GetHashCode();
        public override string ToString() => Handle != 0 ? Path : "None";

        public static bool operator ==(InputAction left, InputAction right) => left.Equals(right);
        public static bool operator !=(InputAction left, InputAction right) => !left.Equals(right);
    }

    // What handlers are told: the action, the player, and its value for that player
    public readonly struct InputActionEvent
    {
        public InputAction Action { get; }
        public int Player { get; }
        public float Value { get; }

        public InputActionEvent(InputAction action, int player, float value)
        {
            Action = action;
            Player = player;
            Value = value;
        }
    }
}