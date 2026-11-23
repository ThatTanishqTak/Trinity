using System;
using Trinity.Interop;

namespace Trinity
{
    // Queries see bodies as the last step left them. All forms fill the caller's span, nearest first
    public static unsafe class Physics
    {
        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out PhysicsHit hit, QueryFilter? filter = null)
        {
            QueryFilter l_Filter = filter.GetValueOrDefault();
            PhysicsHit l_Hit;
            bool l_Met = Native.Physics_Raycast(&origin, &direction, maxDistance, filter.HasValue ? &l_Filter : null, false, &l_Hit, 1) > 0;
            hit = l_Met ? l_Hit : default;

            return l_Met;
        }

        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, QueryFilter? filter = null) => Raycast(origin, direction, maxDistance, out _, filter);

        public static int RaycastAll(Vector3 origin, Vector3 direction, float maxDistance, Span<PhysicsHit> hits, QueryFilter? filter = null)
        {
            QueryFilter l_Filter = filter.GetValueOrDefault();
            fixed (PhysicsHit* l_Hits = hits)
            {
                return Native.Physics_Raycast(&origin, &direction, maxDistance, filter.HasValue ? &l_Filter : null, true, l_Hits, hits.Length);
            }
        }

        // The shape swept from where it is placed, as for a ray
        public static bool ShapeCast(QueryShape shape, Vector3 position, Quaternion rotation, Vector3 direction, float maxDistance, out PhysicsHit hit, QueryFilter? filter = null)
        {
            QueryFilter l_Filter = filter.GetValueOrDefault();
            PhysicsHit l_Hit;
            bool l_Met = CheckShape(Native.Physics_ShapeCast(&shape, &position, &rotation, &direction, maxDistance, filter.HasValue ? &l_Filter : null, false, &l_Hit, 1), shape.Type) > 0;
            hit = l_Met ? l_Hit : default;

            return l_Met;
        }

        public static int ShapeCastAll(QueryShape shape, Vector3 position, Quaternion rotation, Vector3 direction, float maxDistance, Span<PhysicsHit> hits, QueryFilter? filter = null)
        {
            QueryFilter l_Filter = filter.GetValueOrDefault();
            fixed (PhysicsHit* l_Hits = hits)
            {
                return CheckShape(Native.Physics_ShapeCast(&shape, &position, &rotation, &direction, maxDistance, filter.HasValue ? &l_Filter : null, true, l_Hits, hits.Length), shape.Type);
            }
        }

        // Every collider the shape overlaps where it is placed, each once, in UUID order, entities only
        public static int Overlap(QueryShape shape, Vector3 position, Quaternion rotation, Span<PhysicsHit> hits, QueryFilter? filter = null)
        {
            QueryFilter l_Filter = filter.GetValueOrDefault();
            fixed (PhysicsHit* l_Hits = hits)
            {
                return CheckShape(Native.Physics_Overlap(&shape, &position, &rotation, filter.HasValue ? &l_Filter : null, l_Hits, hits.Length), shape.Type);
            }
        }

        private static int CheckShape(int count, QueryShapeType type)
        {
            return count >= 0 ? count : throw new ArgumentOutOfRangeException("shape", $"{(int)type} is not a QueryShapeType");
        }
    }

    // As the 3D queries, in the XY plane, with a shape turned counter-clockwise by its angle in radians
    public static unsafe class Physics2D
    {
        public static bool Raycast(Vector2 origin, Vector2 direction, float maxDistance, out PhysicsHit2D hit, QueryFilter? filter = null)
        {
            QueryFilter l_Filter = filter.GetValueOrDefault();
            PhysicsHit2D l_Hit;
            bool l_Met = Native.Physics2D_Raycast(&origin, &direction, maxDistance, filter.HasValue ? &l_Filter : null, false, &l_Hit, 1) > 0;
            hit = l_Met ? l_Hit : default;

            return l_Met;
        }

        public static bool Raycast(Vector2 origin, Vector2 direction, float maxDistance, QueryFilter? filter = null) => Raycast(origin, direction, maxDistance, out _, filter);

        public static int RaycastAll(Vector2 origin, Vector2 direction, float maxDistance, Span<PhysicsHit2D> hits, QueryFilter? filter = null)
        {
            QueryFilter l_Filter = filter.GetValueOrDefault();
            fixed (PhysicsHit2D* l_Hits = hits)
            {
                return Native.Physics2D_Raycast(&origin, &direction, maxDistance, filter.HasValue ? &l_Filter : null, true, l_Hits, hits.Length);
            }
        }

        public static bool ShapeCast(QueryShape2D shape, Vector2 position, float angle, Vector2 direction, float maxDistance, out PhysicsHit2D hit, QueryFilter? filter = null)
        {
            QueryFilter l_Filter = filter.GetValueOrDefault();
            PhysicsHit2D l_Hit;
            bool l_Met = CheckShape(Native.Physics2D_ShapeCast(&shape, &position, angle, &direction, maxDistance, filter.HasValue ? &l_Filter : null, false, &l_Hit, 1), shape.Type) > 0;
            hit = l_Met ? l_Hit : default;

            return l_Met;
        }

        public static int ShapeCastAll(QueryShape2D shape, Vector2 position, float angle, Vector2 direction, float maxDistance, Span<PhysicsHit2D> hits, QueryFilter? filter = null)
        {
            QueryFilter l_Filter = filter.GetValueOrDefault();
            fixed (PhysicsHit2D* l_Hits = hits)
            {
                return CheckShape(Native.Physics2D_ShapeCast(&shape, &position, angle, &direction, maxDistance, filter.HasValue ? &l_Filter : null, true, l_Hits, hits.Length), shape.Type);
            }
        }

        public static int Overlap(QueryShape2D shape, Vector2 position, float angle, Span<PhysicsHit2D> hits, QueryFilter? filter = null)
        {
            QueryFilter l_Filter = filter.GetValueOrDefault();
            fixed (PhysicsHit2D* l_Hits = hits)
            {
                return CheckShape(Native.Physics2D_Overlap(&shape, &position, angle, filter.HasValue ? &l_Filter : null, l_Hits, hits.Length), shape.Type);
            }
        }

        private static int CheckShape(int count, QueryShape2DType type)
        {
            return count >= 0 ? count : throw new ArgumentOutOfRangeException("shape", $"{(int)type} is not a QueryShape2DType");
        }
    }
}