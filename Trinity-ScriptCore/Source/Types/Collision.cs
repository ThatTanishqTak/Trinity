namespace Trinity
{
    // What a physics callback is told: the other entity, or none for a joint to the world, and for a contact beginning where it began and its normal, out from this script's entity towards the other. A 2D event's point and normal have 0 in Z
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