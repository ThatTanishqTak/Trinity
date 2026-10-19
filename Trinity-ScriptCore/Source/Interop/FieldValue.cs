using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace Trinity.Interop
{
    // A script field's value as it crosses to the engine, laid out as the engine's Interop::FieldValue, which it checks at start. A bool, the integers, an enum and the UUID of an entity or an asset are in Integer, a float and a double in Real, the vectors, a quaternion and a colour in X to W, and a string and an input action's path in Text. The generated code of each script class makes and reads these
    [EditorBrowsable(EditorBrowsableState.Never)]
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct FieldValue
    {
        public FieldType Type;
        public int Reserved;
        public long Integer;
        public double Real;
        public float X;
        public float Y;
        public float Z;
        public float W;
        public Utf8 Text;

        public static FieldValue Bool(bool value) => new() { Type = FieldType.Bool, Integer = value ? 1 : 0 };
        public static FieldValue Int32(int value) => new() { Type = FieldType.Int32, Integer = value };
        public static FieldValue Int64(long value) => new() { Type = FieldType.Int64, Integer = value };
        public static FieldValue UInt32(uint value) => new() { Type = FieldType.UInt32, Integer = value };
        public static FieldValue UInt64(ulong value) => new() { Type = FieldType.UInt64, Integer = unchecked((long)value) };
        public static FieldValue Float(float value) => new() { Type = FieldType.Float, Real = value };
        public static FieldValue Double(double value) => new() { Type = FieldType.Double, Real = value };
        public static FieldValue Vector2(Vector2 value) => new() { Type = FieldType.Vector2, X = value.X, Y = value.Y };
        public static FieldValue Vector3(Vector3 value) => new() { Type = FieldType.Vector3, X = value.X, Y = value.Y, Z = value.Z };
        public static FieldValue Vector4(Vector4 value) => new() { Type = FieldType.Vector4, X = value.X, Y = value.Y, Z = value.Z, W = value.W };
        public static FieldValue Quaternion(Quaternion value) => new() { Type = FieldType.Quaternion, X = value.X, Y = value.Y, Z = value.Z, W = value.W };
        public static FieldValue Color(Color value) => new() { Type = FieldType.Color, X = value.R, Y = value.G, Z = value.B, W = value.A };
        public static FieldValue Enum(long value) => new() { Type = FieldType.Enum, Integer = value };
        public static FieldValue Entity(Entity value) => new() { Type = FieldType.Entity, Integer = unchecked((long)value.ID) };
        public static FieldValue Asset(ulong id) => new() { Type = FieldType.Asset, Integer = unchecked((long)id) };

        // Encoded into a buffer of ScriptCore's, valid until the next string is made, which is long enough for the engine to copy it
        public static FieldValue String(string? value) => Encode(FieldType.String, value);

        // By the path it was found by, which an action found while no scene ran keeps too
        public static FieldValue InputAction(InputAction value) => Encode(FieldType.InputAction, value.Path);

        public readonly bool ToBool() => Integer != 0;
        public readonly int ToInt32() => unchecked((int)Integer);
        public readonly long ToInt64() => Integer;
        public readonly uint ToUInt32() => unchecked((uint)Integer);
        public readonly ulong ToUInt64() => unchecked((ulong)Integer);
        public readonly float ToFloat() => (float)Real;
        public readonly double ToDouble() => Real;
        public readonly Vector2 ToVector2() => new(X, Y);
        public readonly Vector3 ToVector3() => new(X, Y, Z);
        public readonly Vector4 ToVector4() => new(X, Y, Z, W);
        public readonly Quaternion ToQuaternion() => new(X, Y, Z, W);
        public readonly Color ToColor() => new(X, Y, Z, W);
        public readonly long ToEnum() => Integer;
        public readonly Entity ToEntity() => new(unchecked((ulong)Integer));
        public readonly ulong ToAsset() => unchecked((ulong)Integer);
        public readonly string ToText() => Text.Data != null && Text.Length > 0 ? Encoding.UTF8.GetString(Text.Data, Text.Length) : string.Empty;

        // Found as the script is given its fields, and None for an empty path
        public readonly InputAction ToInputAction()
        {
            string l_Path = ToText();

            return l_Path.Length > 0 ? Trinity.InputAction.Find(l_Path) : Trinity.InputAction.None;
        }

        private static FieldValue Encode(FieldType type, string? value)
        {
            value ??= string.Empty;
            byte* l_Buffer = StringBuffer.Get(Encoding.UTF8.GetMaxByteCount(value.Length));
            int l_Length = Encoding.UTF8.GetBytes(value, new Span<byte>(l_Buffer, Encoding.UTF8.GetMaxByteCount(value.Length)));

            return new() { Type = type, Text = new Utf8(l_Buffer, l_Length) };
        }

        // A pinned buffer, which grows to the longest string asked for and never moves while it lasts
        private static class StringBuffer
        {
            private static byte[] s_Buffer = GC.AllocateUninitializedArray<byte>(256, pinned: true);

            public static byte* Get(int size)
            {
                if (size > s_Buffer.Length)
                {
                    s_Buffer = GC.AllocateUninitializedArray<byte>(Math.Max(size, s_Buffer.Length * 2), pinned: true);
                }

                return (byte*)System.Runtime.CompilerServices.Unsafe.AsPointer(ref MemoryMarshal.GetArrayDataReference(s_Buffer));
            }
        }
    }
}