using System;
using System.Runtime.InteropServices;

namespace Trinity
{
    // What a query may hit: colliders on the layers in the mask, one bit for each of the project's 16 layers, triggers only when it asks for them, and nothing of the entity it passes over, such as the one asking, whose body and colliders are all left out. Laid out as the engine's Interop::QueryFilter, which it checks at start. A query given none hits every layer and no triggers, as new QueryFilter() does, while default has no layers and hits nothing
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

        // Every layer, leaving out the entity, such as the script's own, so a ray from inside its collider does not meet it
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

    // A shape a 3D query casts or overlaps, sized as the colliders are: a sphere's radius, a box's half extents, and a capsule's radius and whole height along its Y axis, end caps included. Laid out as the engine's Interop::QueryShape
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

    // A shape a 2D query casts or overlaps, sized as the 2D colliders are: a circle's radius, a box's half extents, and a capsule's radius and whole height along its Y axis. Laid out as the engine's Interop::QueryShape2D
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

    // A collider a query met: its entity and the entity whose body it is part of, which are one for a collider on the body's own entity. A cast's hit has where it met the collider, the collider's normal there, out towards the ray or shape, and how far along the cast it was. A cast that starts inside a solid meets it at once, at the start, with the normal against the cast. An overlap has only the entities. Laid out as the engine's Interop::PhysicsHit, so a query writes straight into the caller's span
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

    // A 2D collider a query met, as a 3D one, in the XY plane. Laid out as the engine's Interop::PhysicsHit2D
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