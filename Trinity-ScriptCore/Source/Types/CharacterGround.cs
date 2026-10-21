namespace Trinity
{
    // What a character stands on as its last step left it, as the engine's CharacterGround3D: whether it is ground it can stand on, and, on any ground, a slope too steep included, the ground's normal, how fast the ground moves where the character is, and the entity whose body it is, None for ground that is no body of the scene's
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

    // What a 2D character stands on, as a 3D one's, in the XY plane, with how fast the ground moves at the middle of the character's lower end cap
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