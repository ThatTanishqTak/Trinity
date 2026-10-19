using System;
using Trinity.Interop;

namespace Trinity
{
    // An action of the running scene's actions asset, as the engine's InputAction is: found once by its path, Map/Action, or by its name alone while only one map has it, as OnCreate does, and read each frame for nothing. One found before the asset is ready, or for an action it lacks, reads as released and 0 until the asset has it, as after a reload, and one found while no scene runs reads nothing. Pressed and released are the action going past the press point and back, and performed and canceled are its interaction's, such as a Hold, which the asset gives it. In OnFixedUpdate, WasPressed, WasReleased, WasPerformed and WasCanceled are what happened since the step before, so each is seen by exactly one fixed step whatever the frame rate. Elsewhere they are this frame's. Each read is for a player, player 0 when none is named. A script's field of this type is set in Properties to an action of the project's default asset, and is found as the script is given its fields. Two are equal when found by the same path in the same run of a scene. Main thread only
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

        // Its interaction completing, and failing once begun, read as WasPressed is. One with no interaction performs as it is pressed and never cancels
        public bool WasPerformed(int player = 0) => Native.InputAction_WasPerformed(Handle, player);
        public bool WasCanceled(int player = 0) => Native.InputAction_WasCanceled(Handle, player);

        // How far its interaction has come, from 0 to 1: a Hold's time held over its Duration, staying 1 once it performs until it is released, a MultiTap's taps done over its Count, and 1 in the frame a Tap or MultiTap performs. One with no interaction is 1 while pressed
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

        // Its bindings as the asset has them, each a single source, or a composite of 2 or 4 parts in the order its Composite gives them, Negative and Positive, or Up, Down, Left and Right. 0 for an action the asset lacks
        public int BindingCount => Native.InputAction_GetBindingCount(Handle);
        public int GetPartCount(int binding) => Native.InputAction_GetPartCount(Handle, binding);

        // A part's source for a player by its name, such as Keyboard/Space or Gamepad/A, from the player's rebind where it has one and from the asset otherwise, and the asset's alone. Empty for none, and for a part there is not. A new string each time, so a controls menu reads it as it opens and after each rebind, not every frame
        public string GetSource(int binding, int part = 0, int player = 0) => Native.InputAction_GetSource(Handle, binding, part, player, false).ToString();
        public string GetDefaultSource(int binding, int part = 0) => Native.InputAction_GetSource(Handle, binding, part, 0, true).ToString();

        // Rebinds a part for a player at once to a source by its name, empty for none, and saves the player's rebinds under /saves, so the asset never changes. Refused when another binding or part of the action's map already reads the source for the player, and rebinding to the asset's own source takes the rebind away. A player slot keeps its rebinds whether or not the player is in use. None, and logged, for a name, binding, part or player there is not
        public unsafe InputRebind Rebind(int binding, int part, string source, int player = 0)
        {
            ArgumentNullException.ThrowIfNull(source);
            using Utf8String l_Source = new(source, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Source.Bytes)
            {
                return new InputRebind(Native.InputAction_Rebind(Handle, binding, part, new Utf8(l_Data, l_Source.Bytes.Length), player));
            }
        }

        // Listens from the next frame for the next key but Escape, mouse button or gamepad button of the devices the player reads, or stick or trigger pushed past half, and rebinds the part to it as Rebind does. One already pushed as it starts is taken only once it has come back. A stick is taken whole for a Vector2 action's own binding, and as the axis pushed otherwise. While it waits the player's actions read as released, and once it has taken an input, or Escape, each reads again only after its sources are let go, so that press reaches none. Escape and Cancel stop it, and so does another starting. A coroutine waits for it with yield return new WaitUntil(() => rebind.IsDone). None, and logged, as for Rebind, and for a player there is not
        public InputRebind StartRebind(int binding, int part = 0, int player = 0) => new(Native.InputAction_StartRebind(Handle, binding, part, player));

        // Back to the asset's source, saved as Rebind is
        public void ResetBinding(int binding, int part = 0, int player = 0) => Native.InputAction_ResetBinding(Handle, binding, part, player);

        // Once a frame for each player it went down for, as WasPressed reads in OnUpdate, after the OnCreate of scripts made that frame and before the fixed steps, so a handler added in OnCreate hears that frame's press. A player's events of one frame come Pressed, Released, Performed, then Canceled, so a press and a release in one frame raise both, Pressed first, and a Tap performs after its release. A handler that is a method of a script, or a lambda that uses nothing but the script's own members, goes when the script does; any other stays until it is removed, so a script removes it in OnDestroy. A handler that throws is logged and removed while play goes on. A reload removes every handler, so a script made again adds its own in OnReloaded. Adding one throws for None, and for an action found while no scene ran
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

        // As Pressed, for each player its interaction failed for once begun, as WasCanceled reads in OnUpdate
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

    // What an action's handlers are told: the action, the player it went down, up, performed or canceled for, and its value for that player as it did, as ReadValue gives it
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