namespace Trinity
{
    // The engine's asset types, by the names the engine gives them, for AssetRef<T>. None is made, since each only names its type
    public sealed class TextureAsset : IAsset
    {
        private TextureAsset() { }

        public static string AssetType => "Texture";
    }

    public sealed class MeshAsset : IAsset
    {
        private MeshAsset() { }

        public static string AssetType => "Mesh";
    }

    public sealed class MaterialAsset : IAsset
    {
        private MaterialAsset() { }

        public static string AssetType => "Material";
    }

    public sealed class EnvironmentAsset : IAsset
    {
        private EnvironmentAsset() { }

        public static string AssetType => "Environment";
    }

    public sealed class ConvexHullAsset : IAsset
    {
        private ConvexHullAsset() { }

        public static string AssetType => "ConvexHull";
    }

    public sealed class CollisionMeshAsset : IAsset
    {
        private CollisionMeshAsset() { }

        public static string AssetType => "CollisionMesh";
    }

    public sealed class SceneAsset : IAsset
    {
        private SceneAsset() { }

        public static string AssetType => "Scene";
    }

    // A prefab or a model, which is read as a prefab of its hierarchy, so a field of AssetRef<PrefabAsset> takes either, for Entity.Instantiate
    public sealed class PrefabAsset : IAsset
    {
        private PrefabAsset() { }

        public static string AssetType => "Prefab";
    }

    public sealed class BinaryAsset : IAsset
    {
        private BinaryAsset() { }

        public static string AssetType => "Binary";
    }

    public sealed class InputActionsAsset : IAsset
    {
        private InputActionsAsset() { }

        public static string AssetType => "InputActions";
    }
}