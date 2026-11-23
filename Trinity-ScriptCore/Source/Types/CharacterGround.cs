namespace Trinity
{
    // As the engine's CharacterGround3D: whether it can stand, the normal, the ground's speed, entity
    public readonly struct CharacterGround
    {
        public bool Grounded { get; }
        public Vector3 Normal { get; }
        public Vector3 Velocity { get; }
        public Entity Entity { get; }

        public CharacterGround(bool grounded, Vector3 normal, Vector3 velocity, Entity entity)
        {
            Grounded = grounded;
            Normal = normal;
            Velocity = velocity;
            Entity = entity;
        }
    }

    // What a 2D character stands on, as a 3D one's, in the XY plane
    public readonly struct CharacterGround2D
    {
        public bool Grounded { get; }
        public Vector2 Normal { get; }
        public Vector2 Velocity { get; }
        public Entity Entity { get; }

        public CharacterGround2D(bool grounded, Vector2 normal, Vector2 velocity, Entity entity)
        {
            Grounded = grounded;
            Normal = normal;
            Velocity = velocity;
            Entity = entity;
        }
    }
}