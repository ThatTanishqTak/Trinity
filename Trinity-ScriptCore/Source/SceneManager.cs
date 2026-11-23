using Trinity.Interop;

namespace Trinity
{
    // Switches at the end of the frame once the scene has loaded. False, logged, for an unknown scene
    public static class SceneManager
    {
        public static bool Load(AssetRef<SceneAsset> scene) => Native.SceneManager_Load(scene.ID);

        public static unsafe bool Load(string path)
        {
            using Utf8String l_Path = new(path, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Path.Bytes)
            {
                return Native.SceneManager_LoadPath(new Utf8(l_Data, l_Path.Bytes.Length));
            }
        }

        public static bool IsLoading => Native.SceneManager_IsLoading();

        // The scene asset running, or None for a scene from no asset, such as an unsaved one in Forge
        public static AssetRef<SceneAsset> Current => new(Native.SceneManager_GetScene());
    }
}