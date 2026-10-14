using System;
using System.Globalization;
using System.Runtime.InteropServices;

namespace Trinity
{
    // Laid out as glm::vec3, which the engine checks at start
    [StructLayout(LayoutKind.Sequential)]
    public struct Vector3 : IEquatable<Vector3>
    {
        public float X;
        public float Y;
        public float Z;

        public static Vector3 Zero => default;
        public static Vector3 One => new(1.0f, 1.0f, 1.0f);
        public static Vector3 Up => new(0.0f, 1.0f, 0.0f);

        public Vector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public readonly float Length => MathF.Sqrt(X * X + Y * Y + Z * Z);

        public static Vector3 operator +(Vector3 left, Vector3 right) => new(left.X + right.X, left.Y + right.Y, left.Z + right.Z);
        public static Vector3 operator -(Vector3 left, Vector3 right) => new(left.X - right.X, left.Y - right.Y, left.Z - right.Z);
        public static Vector3 operator -(Vector3 value) => new(-value.X, -value.Y, -value.Z);
        public static Vector3 operator *(Vector3 value, float scale) => new(value.X * scale, value.Y * scale, value.Z * scale);
        public static Vector3 operator *(float scale, Vector3 value) => value * scale;
        public static Vector3 operator /(Vector3 value, float divisor) => new(value.X / divisor, value.Y / divisor, value.Z / divisor);
        public static bool operator ==(Vector3 left, Vector3 right) => left.Equals(right);
        public static bool operator !=(Vector3 left, Vector3 right) => !left.Equals(right);

        public readonly bool Equals(Vector3 other) => X == other.X && Y == other.Y && Z == other.Z;
        public override readonly bool Equals(object? obj) => obj is Vector3 other && Equals(other);
        public override readonly int GetHashCode() => HashCode.Combine(X, Y, Z);
        public override readonly string ToString() => string.Create(CultureInfo.InvariantCulture, $"({X}, {Y}, {Z})");
    }
}