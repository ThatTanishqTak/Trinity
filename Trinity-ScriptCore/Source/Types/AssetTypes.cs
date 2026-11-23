namespace Trinity
{
    // The engine's asset types, by the engine's names, for AssetRef<T>. None is ever made
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

    // A prefab or a model, which reads as a prefab of its hierarchy, for Entity.Instantiate
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

    // What an AudioSource plays, as Unity 6's AudioResource: an AudioClip or an Audio Random Container
    public interface IAudioResource : IAsset
    {
    }

    public sealed class AudioClipAsset : IAudioResource
    {
        private AudioClipAsset() { }

        public static string AssetType => "AudioClip";
    }

    public sealed class SoundEventAsset : IAudioResource
    {
        private SoundEventAsset() { }

        public static string AssetType => "SoundEvent";
    }

    // An AudioClip or a Sound Event, so a field of AssetRef<AudioResourceAsset> takes either
    public sealed class AudioResourceAsset : IAudioResource
    {
        private AudioResourceAsset() { }

        public static string AssetType => "Audio Resource";
    }

    // The project's mixer is the only one its scenes play through, so a field of this names it
    public sealed class AudioMixerAsset : IAsset
    {
        private AudioMixerAsset() { }

        public static string AssetType => "AudioMixer";
    }
}