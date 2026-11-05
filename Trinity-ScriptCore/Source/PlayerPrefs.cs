using Trinity.Interop;

namespace Trinity
{
    // As Unity's PlayerPrefs: small values a game keeps on the player's machine from one run to the next, each an int, a float or a string under a key, the engine's own, so C++ and C# share them. They are written to their file on Save and as the game closes. A key holds one kind at a time, setting it as another replaces it, and reading it as another gives the default. A player keeps them in its saves, and Forge's Play in the project's Saved folder. Main thread only
    public static unsafe class PlayerPrefs
    {
        public static void SetInt(string key, int value)
        {
            using Utf8String l_Key = new(key, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Key.Bytes)
            {
                Native.PlayerPrefs_SetInt(new Utf8(l_Data, l_Key.Bytes.Length), value);
            }
        }

        public static int GetInt(string key, int defaultValue = 0)
        {
            using Utf8String l_Key = new(key, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Key.Bytes)
            {
                return Native.PlayerPrefs_GetInt(new Utf8(l_Data, l_Key.Bytes.Length), defaultValue);
            }
        }

        public static void SetFloat(string key, float value)
        {
            using Utf8String l_Key = new(key, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Key.Bytes)
            {
                Native.PlayerPrefs_SetFloat(new Utf8(l_Data, l_Key.Bytes.Length), value);
            }
        }

        public static float GetFloat(string key, float defaultValue = 0.0f)
        {
            using Utf8String l_Key = new(key, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Key.Bytes)
            {
                return Native.PlayerPrefs_GetFloat(new Utf8(l_Data, l_Key.Bytes.Length), defaultValue);
            }
        }

        public static void SetString(string key, string value)
        {
            using Utf8String l_Key = new(key, stackalloc byte[Utf8String.c_StackBytes]);
            using Utf8String l_Value = new(value, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_KeyData = l_Key.Bytes)
            fixed (byte* l_ValueData = l_Value.Bytes)
            {
                Native.PlayerPrefs_SetString(new Utf8(l_KeyData, l_Key.Bytes.Length), new Utf8(l_ValueData, l_Value.Bytes.Length));
            }
        }

        // A copy, read before the default's bytes go
        public static string GetString(string key, string defaultValue = "")
        {
            using Utf8String l_Key = new(key, stackalloc byte[Utf8String.c_StackBytes]);
            using Utf8String l_Default = new(defaultValue, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_KeyData = l_Key.Bytes)
            fixed (byte* l_DefaultData = l_Default.Bytes)
            {
                return Native.PlayerPrefs_GetString(new Utf8(l_KeyData, l_Key.Bytes.Length), new Utf8(l_DefaultData, l_Default.Bytes.Length)).ToString();
            }
        }

        public static bool HasKey(string key)
        {
            using Utf8String l_Key = new(key, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Key.Bytes)
            {
                return Native.PlayerPrefs_HasKey(new Utf8(l_Data, l_Key.Bytes.Length));
            }
        }

        public static void DeleteKey(string key)
        {
            using Utf8String l_Key = new(key, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Key.Bytes)
            {
                Native.PlayerPrefs_DeleteKey(new Utf8(l_Data, l_Key.Bytes.Length));
            }
        }

        public static void DeleteAll() => Native.PlayerPrefs_DeleteAll();

        // Writes the file now when anything changed. False, logged, when it cannot be written, and the values stay to be saved again
        public static bool Save() => Native.PlayerPrefs_Save();
    }
}