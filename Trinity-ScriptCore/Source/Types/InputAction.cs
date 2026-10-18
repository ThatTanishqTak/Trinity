using System;
using Trinity.Interop;

namespace Trinity
{
    // An action of the running scene's actions asset, as the engine's InputAction is: found once by its path, Map/Action, or by its name alone while only one map has it, as OnCreate does, and read each frame for nothing. One found before the asset is ready, or for an action it lacks, reads as released and 0 until the asset has it, as after a reload, and one found while no scene runs reads nothing. In OnFixedUpdate, WasPressed and WasReleased are what happened since the step before, so each press is seen by exactly one fixed step whatever the frame rate. Elsewhere they are this frame's. Each read is for a player, player 0 when none is named. Two are equal when found by the same path in the same run of a scene. Main thread only
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

        // A Button's strength from 0 to 1, an Axis from -1 to 1, and a Vector2's length
        public float ReadValue(int player = 0) => Native.InputAction_ReadValue(Handle, player);

        // A Vector2's X and Y, and a Button's or an Axis's value as X, with Y 0
        public unsafe Vector2 ReadVector2(int player = 0)
        {
            Vector2 l_Value;
            Native.InputAction_ReadVector2(Handle, player, &l_Value);

            return l_Value;
        }

        // Once a frame for each player it went down for, as WasPressed reads in OnUpdate, after the OnCreate of scripts made that frame and before the fixed steps, so a handler added in OnCreate hears that frame's press. A press and a release in one frame raise both, Pressed first. A handler that is a method of a script, or a lambda that uses nothing but the script's own members, goes when the script does; any other stays until it is removed, so a script removes it in OnDestroy. A handler that throws is logged and removed while play goes on. A reload removes every handler, so a script made again adds its own in OnReloaded. Adding one throws for None, and for an action found while no scene ran
        public event Action<InputActionEvent> Pressed
        {
            add => InputEvents.Add(this, false, value);
            remove => InputEvents.Remove(this, false, value);
        }

        // As Pressed, for each player it went up for, as WasReleased reads in OnUpdate
        public event Action<InputActionEvent> Released
        {
            add => InputEvents.Add(this, true, value);
            remove => InputEvents.Remove(this, true, value);
        }

        public bool Equals(InputAction other) => Handle == other.Handle;
        public override bool Equals(object? obj) => obj is InputAction other && Equals(other);
        public override int GetHashCode() => Handle.GetHashCode();
        public override string ToString() => Handle != 0 ? Path : "None";

        public static bool operator ==(InputAction left, InputAction right) => left.Equals(right);
        public static bool operator !=(InputAction left, InputAction right) => !left.Equals(right);
    }

    // What an action's Pressed and Released handlers are told: the action, the player it went down or up for, and its value for that player as it did, as ReadValue gives it
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