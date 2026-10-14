using System;

namespace Trinity
{
    // A kind of asset, which names its type as the engine does, so a field of AssetRef<T> offers only assets of that type
    public interface IAsset
    {
        static abstract string AssetType { get; }
    }

    // An asset of type T, named by its UUID. None has the UUID 0
    public readonly struct AssetRef<T> : IEquatable<AssetRef<T>> where T : IAsset
    {
        public static AssetRef<T> None => default;

        public ulong ID { get; }

        public bool IsValid => ID != 0;

        public AssetRef(ulong id)
        {
            ID = id;
        }

        public bool Equals(AssetRef<T> other) => ID == other.ID;
        public override bool Equals(object? obj) => obj is AssetRef<T> other && Equals(other);
        public override int GetHashCode() => ID.GetHashCode();
        public override string ToString() => ID.ToString("x16");

        public static bool operator ==(AssetRef<T> left, AssetRef<T> right) => left.Equals(right);
        public static bool operator !=(AssetRef<T> left, AssetRef<T> right) => !left.Equals(right);
    }
}