using System;

namespace Trinity
{
    // An entity, named by its UUID, so it stays the same entity through a reload and in a scene that was saved and loaded again. None has the UUID 0
    public readonly struct Entity : IEquatable<Entity>
    {
        public static Entity None => default;

        public ulong ID { get; }

        public bool IsValid => ID != 0;

        public Entity(ulong id)
        {
            ID = id;
        }

        public bool Equals(Entity other) => ID == other.ID;
        public override bool Equals(object? obj) => obj is Entity other && Equals(other);
        public override int GetHashCode() => ID.GetHashCode();
        public override string ToString() => ID.ToString("x16");

        public static bool operator ==(Entity left, Entity right) => left.Equals(right);
        public static bool operator !=(Entity left, Entity right) => !left.Equals(right);
    }
}