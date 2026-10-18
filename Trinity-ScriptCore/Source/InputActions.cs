using Trinity.Interop;

namespace Trinity
{
    // A device a player reads: the keyboard and mouse together, or the gamepad in one of Input's slots, numbered as the engine's InputDevice numbers it
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

    // The players the running scene reads actions for, the maps each has enabled and the devices each reads, as the engine's InputActions has them. The scene's actions asset is the project's default in Forge's Play. Player 0 always is, has the first map enabled, and reads every device no other player has been assigned, so one player needs nothing set up. Players are numbered from 0, and NoPlayer is none. Main thread only
    public static unsafe class InputActions
    {
        public const int NoPlayer = -1;

        public static int MaxPlayers => Native.InputActions_GetMaxPlayers();

        // How far a Button or an Axis, or a Vector2's length, has to go for the action to be pressed
        public static float PressPoint => Native.InputActions_GetPressPoint();

        // None when the running scene has none
        public static AssetRef<InputActionsAsset> Asset => new(Native.InputActions_GetAsset());
        public static bool IsReady => Native.InputActions_IsReady();

        // By name. A map the asset does not have, or a player there is not, is false and logged. Before the asset is ready the name is kept and checked once it is. A disabled map's actions read as released from the next frame, so a held one is released once
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

        // The lowest player not in use, with the first map enabled and no device, so it reads nothing until one is assigned. NoPlayer when all MaxPlayers are, or no scene runs
        public static int AddPlayer() => Native.InputActions_AddPlayer();
        // Its devices go back to player 0, which is never removed
        public static void RemovePlayer(int player) => Native.InputActions_RemovePlayer(player);
        public static bool IsPlayer(int player) => Native.InputActions_IsPlayer(player);
        public static int PlayerCount => Native.InputActions_GetPlayerCount();

        // A device is one player's at a time, so assigning it takes it from any other. A gamepad keeps its player while it is disconnected, and the same pad reconnecting into its slot is that player's again. Assigning player 0 its own devices keeps them from joining as another
        public static bool AssignDevice(InputDevice device, int player) => Native.InputActions_AssignDevice((int)device, player);
        public static void UnassignDevice(InputDevice device) => Native.InputActions_UnassignDevice((int)device);
        // The player assigned the device, or NoPlayer for one none has, which player 0 reads
        public static int GetDevicePlayer(InputDevice device) => Native.InputActions_GetDevicePlayer((int)device);
        public static bool ReadsDevice(int player, InputDevice device) => Native.InputActions_ReadsDevice(player, (int)device);

        // A device no player has been assigned that had a key or button go down this frame, the keyboard and mouse first and then the gamepads by slot, or None, for a game's press to join
        public static InputDevice JoiningDevice => (InputDevice)Native.InputActions_GetJoiningDevice();

        // The device of the gamepad in Input's slot, or None for a slot there is not
        public static InputDevice GetGamepadDevice(int gamepad) => (uint)gamepad < (uint)Input.MaxGamepads ? (InputDevice)(gamepad + 1) : InputDevice.None;
    }
}