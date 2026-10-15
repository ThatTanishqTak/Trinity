using Trinity.Interop;

namespace Trinity
{
    // The running scene's switch to another scene asset, by its reference, which a script field of AssetRef<SceneAsset> picks, or by its path in the project, such as Scenes/Level.trscene. It comes at the end of the frame once the scene and every asset it uses have loaded: entities with KeepAcrossScenes move into it with their subtrees and scripts, the old scene's scripts have OnDestroy, and the new scene's have OnCreate. Load is false, logged, for a scene there is not, and this scene goes on. One asked for while another loads takes its place
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

        // The scene asset running, or None for a scene that came from no asset, such as an unsaved one played in Forge
        public static AssetRef<SceneAsset> Current => new(Native.SceneManager_GetScene());
    }
}