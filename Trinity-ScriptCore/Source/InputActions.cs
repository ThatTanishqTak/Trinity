using Trinity.Interop;

namespace Trinity
{
    // The keyboard and mouse together, or a gamepad slot, numbered as the engine's InputDevice
    public enum InputDevice : byte
    {
        KeyboardMouse = 0,
        Gamepad0,
        Gamepad1,
        Gamepad2,
        Gamepad3,
        Gamepad4,
        Gamepad5,
        Gamepad6,
        Gamepad7,

        None = 255
    }

    // The players the scene reads actions for. Player 0 reads every unassigned device. Main thread only
    public static unsafe class InputActions
    {
        public const int NoPlayer = -1;

        public static int MaxPlayers => Native.InputActions_GetMaxPlayers();

        // How far a Button or an Axis, or a Vector2's length, has to go for the action to be pressed
        public static float PressPoint => Native.InputActions_GetPressPoint();

        // None when the running scene has none
        public static AssetRef<InputActionsAsset> Asset => new(Native.InputActions_GetAsset());
        public static bool IsReady => Native.InputActions_IsReady();

        // By name. False, logged, for an unknown map or player. Before the asset is ready the name is kept
        public static bool EnableMap(string map, int player = 0)
        {
            using Utf8String l_Map = new(map, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Map.Bytes)
            {
                return Native.InputActions_EnableMap(new Utf8(l_Data, l_Map.Bytes.Length), player);
            }
        }

        public static bool DisableMap(string map, int player = 0)
        {
            using Utf8String l_Map = new(map, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Map.Bytes)
            {
                return Native.InputActions_DisableMap(new Utf8(l_Data, l_Map.Bytes.Length), player);
            }
        }

        public static bool IsMapEnabled(string map, int player = 0)
        {
            using Utf8String l_Map = new(map, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Map.Bytes)
            {
                return Native.InputActions_IsMapEnabled(new Utf8(l_Data, l_Map.Bytes.Length), player);
            }
        }

        // The lowest free player, with the first map and no device. NoPlayer when full or no scene runs
        public static int AddPlayer() => Native.InputActions_AddPlayer();
        // Its devices go back to player 0, which is never removed
        public static void RemovePlayer(int player) => Native.InputActions_RemovePlayer(player);
        public static bool IsPlayer(int player) => Native.InputActions_IsPlayer(player);
        public static int PlayerCount => Native.InputActions_GetPlayerCount();

        // A device is one player's at a time. A gamepad keeps its player while it is disconnected
        public static bool AssignDevice(InputDevice device, int player) => Native.InputActions_AssignDevice((int)device, player);
        public static void UnassignDevice(InputDevice device) => Native.InputActions_UnassignDevice((int)device);
        // The player assigned the device, or NoPlayer for one none has, which player 0 reads
        public static int GetDevicePlayer(InputDevice device) => Native.InputActions_GetDevicePlayer((int)device);
        public static bool ReadsDevice(int player, InputDevice device) => Native.InputActions_ReadsDevice(player, (int)device);

        // An unassigned device that had a key or button go down this frame, for a game's press to join
        public static InputDevice JoiningDevice => (InputDevice)Native.InputActions_GetJoiningDevice();

        // The device of the gamepad in Input's slot, or None for a slot there is not
        public static InputDevice GetGamepadDevice(int gamepad) => (uint)gamepad < (uint)Input.MaxGamepads ? (InputDevice)(gamepad + 1) : InputDevice.None;

        // Takes away every rebind the player slot has, as a game's Reset to Defaults does, and saves
        public static void ResetBindings(int player = 0) => Native.InputActions_ResetBindings(player);
    }
}