using System;
using System.Runtime.InteropServices;

namespace Trinity
{
    // Laid out as the engine's Interop::QueryFilter. new QueryFilter() hits every layer, default none
    [StructLayout(LayoutKind.Sequential)]
    public struct QueryFilter
    {
        public const uint AllLayers = 0xFFFF;

        public uint Layers;
        public bool HitTriggers;
        public Entity Ignore;

        public static QueryFilter All => new();

        public QueryFilter()
        {
            Layers = AllLayers;
        }

        public QueryFilter(uint layers, bool hitTriggers = false, Entity ignore = default)
        {
            Layers = layers;
            HitTriggers = hitTriggers;
            Ignore = ignore;
        }

        // Every layer, leaving out the entity, so a ray from inside its own collider does not meet it
        public static QueryFilter Ignoring(Entity entity) => new() { Ignore = entity };

        // Only the layers by their numbers in the project
        public static QueryFilter OnLayers(params ReadOnlySpan<int> layers)
        {
            uint l_Mask = 0;
            foreach (int it_Layer in layers)
            {
                l_Mask |= (uint)it_Layer < 16 ? 1u << it_Layer : 0u;
            }

            return new QueryFilter(l_Mask);
        }
    }

    // Sized as the colliders are: a radius, half extents, or a capsule's radius and whole height
    [StructLayout(LayoutKind.Sequential)]
    public struct QueryShape
    {
        public QueryShapeType Type;
        public float Radius;
        public Vector3 HalfExtents;
        public float Height;

        public static QueryShape Sphere(float radius) => new() { Type = QueryShapeType.Sphere, Radius = radius, HalfExtents = new Vector3(radius, radius, radius), Height = radius * 2.0f };
        public static QueryShape Box(Vector3 halfExtents) => new() { Type = QueryShapeType.Box, HalfExtents = halfExtents };
        public static QueryShape Capsule(float radius, float height) => new() { Type = QueryShapeType.Capsule, Radius = radius, HalfExtents = new Vector3(radius, radius, radius), Height = height };
    }

    // A 2D query's shape, sized as the 2D colliders are. Laid out as Interop::QueryShape2D
    [StructLayout(LayoutKind.Sequential)]
    public struct QueryShape2D
    {
        public QueryShape2DType Type;
        public float Radius;
        public Vector2 HalfExtents;
        public float Height;

        public static QueryShape2D Circle(float radius) => new() { Type = QueryShape2DType.Circle, Radius = radius, HalfExtents = new Vector2(radius, radius), Height = radius * 2.0f };
        public static QueryShape2D Box(Vector2 halfExtents) => new() { Type = QueryShape2DType.Box, HalfExtents = halfExtents };
        public static QueryShape2D Capsule(float radius, float height) => new() { Type = QueryShape2DType.Capsule, Radius = radius, HalfExtents = new Vector2(radius, radius), Height = height };
    }

    // Laid out as the engine's Interop::PhysicsHit. An overlap has only the entities
    [StructLayout(LayoutKind.Sequential)]
    public struct PhysicsHit
    {
        public Entity Entity;
        public Entity Body;
        public Vector3 Point;
        public Vector3 Normal;
        public float Distance;
        public bool Trigger;
    }

    // A 2D collider a query met, as a 3D one, in the XY plane. Laid out as Interop::PhysicsHit2D
    [StructLayout(LayoutKind.Sequential)]
    public struct PhysicsHit2D
    {
        public Entity Entity;
        public Entity Body;
        public Vector2 Point;
        public Vector2 Normal;
        public float Distance;
        public bool Trigger;
    }
}