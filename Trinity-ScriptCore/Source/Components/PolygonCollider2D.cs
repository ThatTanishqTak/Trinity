using System;
using Trinity.Interop;

namespace Trinity
{
    // The convex hull of its points, from 3 to 8, in the collider's own space
    public readonly partial struct PolygonCollider2D
    {
        public const int MaxPoints = 8;

        public int PointCount => Native.PolygonCollider2D_GetPointCount(m_Entity.ID);

        public unsafe Vector2 GetPoint(int index)
        {
            Vector2 l_Point;
            if (!Native.PolygonCollider2D_GetPoint(m_Entity.ID, index, &l_Point))
            {
                throw new ArgumentOutOfRangeException(nameof(index), $"The PolygonCollider2D of entity {m_Entity} has no point {index}");
            }

            return l_Point;
        }

        public unsafe void SetPoints(ReadOnlySpan<Vector2> points)
        {
            fixed (Vector2* l_Points = points)
            {
                ComponentAccess.Check(m_Entity, (AccessResult)Native.PolygonCollider2D_SetPoints(m_Entity.ID, l_Points, points.Length), "has no PolygonCollider2D", $"has a PolygonCollider2D, which cannot take {points.Length} points, only 3 to {MaxPoints}");
            }
        }
    }
}