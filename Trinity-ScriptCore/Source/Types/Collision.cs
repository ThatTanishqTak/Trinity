namespace Trinity
{
    // A contact's normal points from this entity to the other, a character hit's towards the character
    public readonly struct Collision
    {
        public Entity Other { get; }
        public Vector3 Point { get; }
        public Vector3 Normal { get; }
        public bool Is2D { get; }

        public Collision(Entity other, Vector3 point, Vector3 normal, bool is2D)
        {
            Other = other;
            Point = point;
            Normal = normal;
            Is2D = is2D;
        }
    }
}