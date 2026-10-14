using System;
using System.Globalization;
using System.Runtime.InteropServices;

namespace Trinity
{
    // Laid out as glm::vec2, which the engine checks at start
    [StructLayout(LayoutKind.Sequential)]
    public struct Vector2 : IEquatable<Vector2>
    {
        public float X;
        public float Y;

        public static Vector2 Zero => default;
        public static Vector2 One => new(1.0f, 1.0f);

        public Vector2(float x, float y)
        {
            X = x;
            Y = y;
        }

        public readonly float Length => MathF.Sqrt(X * X + Y * Y);

        public static Vector2 operator +(Vector2 left, Vector2 right) => new(left.X + right.X, left.Y + right.Y);
        public static Vector2 operator -(Vector2 left, Vector2 right) => new(left.X - right.X, left.Y - right.Y);
        public static Vector2 operator -(Vector2 value) => new(-value.X, -value.Y);
        public static Vector2 operator *(Vector2 value, float scale) => new(value.X * scale, value.Y * scale);
        public static Vector2 operator *(float scale, Vector2 value) => value * scale;
        public static Vector2 operator /(Vector2 value, float divisor) => new(value.X / divisor, value.Y / divisor);
        public static bool operator ==(Vector2 left, Vector2 right) => left.Equals(right);
        public static bool operator !=(Vector2 left, Vector2 right) => !left.Equals(right);

        public readonly bool Equals(Vector2 other) => X == other.X && Y == other.Y;
        public override readonly bool Equals(object? obj) => obj is Vector2 other && Equals(other);
        public override readonly int GetHashCode() => HashCode.Combine(X, Y);
        public override readonly string ToString() => string.Create(CultureInfo.InvariantCulture, $"({X}, {Y})");
    }
}