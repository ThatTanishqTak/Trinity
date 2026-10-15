using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace Trinity.Interop
{
    // ScriptCore's entry points, which the engine finds by name once .NET has started and calls through function pointers. None may throw, since an exception cannot cross into native code
    public static unsafe class Host
    {
        // 0 once the engine's functions are bound, and 1 when the table is not the size this ScriptCore was built for, which means the engine and ScriptCore come from different builds
        [UnmanagedCallersOnly]
        public static int Initialize(NativeFunctions* functions, int size)
        {
            if (functions == null || size != sizeof(NativeFunctions))
            {
                return 1;
            }

            Native.Bind(*functions);

            return 0;
        }

        [UnmanagedCallersOnly]
        public static void Shutdown()
        {
            Native.Bind(default);
        }

        // The runtime's description, such as .NET 10.0.0, written as UTF-8 into the buffer when it fits. Its length in bytes either way
        [UnmanagedCallersOnly]
        public static int GetRuntimeDescription(byte* buffer, int capacity)
        {
            string l_Description = RuntimeInformation.FrameworkDescription;
            int l_Length = Encoding.UTF8.GetByteCount(l_Description);
            if (buffer != null && l_Length <= capacity)
            {
                Encoding.UTF8.GetBytes(l_Description, new Span<byte>(buffer, capacity));
            }

            return l_Length;
        }

        // A call into C# that calls back into the engine: the engine's echo of the value, doubled
        [UnmanagedCallersOnly]
        public static int Handshake(int value)
        {
            return Native.Interop_Echo(value) * 2;
        }

        // The size and field offsets of each type the engine shares with C#, in the order of the engine's check: Vector2, Vector3, Vector4, Quaternion, Color, Matrix4x4, Utf8, FieldValue, Collision, QueryFilter, QueryShape, QueryShape2D, PhysicsHit and PhysicsHit2D. How many were written, or -1 when the engine's TypeLayout is not this one or the buffer is too small
        [UnmanagedCallersOnly]
        public static int DescribeLayouts(TypeLayout* layouts, int capacity, int layoutSize)
        {
            const int c_Count = 14;
            if (layouts == null || layoutSize != sizeof(TypeLayout) || capacity < c_Count)
            {
                return -1;
            }

            Vector2 l_Vector2 = default;
            Vector3 l_Vector3 = default;
            Vector4 l_Vector4 = default;
            Quaternion l_Quaternion = default;
            Color l_Color = default;
            Matrix4x4 l_Matrix = default;
            Utf8 l_Text = default;
            FieldValue l_Value = default;
            Collision l_Collision = default;
            QueryFilter l_Filter = default;
            QueryShape l_Shape = default;
            QueryShape2D l_Shape2D = default;
            PhysicsHit l_Hit = default;
            PhysicsHit2D l_Hit2D = default;

            Describe(ref layouts[0], sizeof(Vector2), At(&l_Vector2, &l_Vector2.X), At(&l_Vector2, &l_Vector2.Y));
            Describe(ref layouts[1], sizeof(Vector3), At(&l_Vector3, &l_Vector3.X), At(&l_Vector3, &l_Vector3.Y), At(&l_Vector3, &l_Vector3.Z));
            Describe(ref layouts[2], sizeof(Vector4), At(&l_Vector4, &l_Vector4.X), At(&l_Vector4, &l_Vector4.Y), At(&l_Vector4, &l_Vector4.Z), At(&l_Vector4, &l_Vector4.W));
            Describe(ref layouts[3], sizeof(Quaternion), At(&l_Quaternion, &l_Quaternion.X), At(&l_Quaternion, &l_Quaternion.Y), At(&l_Quaternion, &l_Quaternion.Z), At(&l_Quaternion, &l_Quaternion.W));
            Describe(ref layouts[4], sizeof(Color), At(&l_Color, &l_Color.R), At(&l_Color, &l_Color.G), At(&l_Color, &l_Color.B), At(&l_Color, &l_Color.A));
            Describe(ref layouts[5], sizeof(Matrix4x4), At(&l_Matrix, &l_Matrix.Column0), At(&l_Matrix, &l_Matrix.Column1), At(&l_Matrix, &l_Matrix.Column2), At(&l_Matrix, &l_Matrix.Column3));
            Describe(ref layouts[6], sizeof(Utf8), At(&l_Text, &l_Text.Data), At(&l_Text, &l_Text.Length));
            Describe(ref layouts[7], sizeof(FieldValue), At(&l_Value, &l_Value.Type), At(&l_Value, &l_Value.Reserved), At(&l_Value, &l_Value.Integer), At(&l_Value, &l_Value.Real), At(&l_Value, &l_Value.X), At(&l_Value, &l_Value.Text));
            Describe(ref layouts[8], sizeof(Collision), At(&l_Collision, &l_Collision.Other), At(&l_Collision, &l_Collision.Point), At(&l_Collision, &l_Collision.Normal), At(&l_Collision, &l_Collision.Is2D));
            Describe(ref layouts[9], sizeof(QueryFilter), At(&l_Filter, &l_Filter.Layers), At(&l_Filter, &l_Filter.HitTriggers), At(&l_Filter, &l_Filter.Ignore));
            Describe(ref layouts[10], sizeof(QueryShape), At(&l_Shape, &l_Shape.Type), At(&l_Shape, &l_Shape.Radius), At(&l_Shape, &l_Shape.HalfExtents), At(&l_Shape, &l_Shape.Height));
            Describe(ref layouts[11], sizeof(QueryShape2D), At(&l_Shape2D, &l_Shape2D.Type), At(&l_Shape2D, &l_Shape2D.Radius), At(&l_Shape2D, &l_Shape2D.HalfExtents), At(&l_Shape2D, &l_Shape2D.Height));
            Describe(ref layouts[12], sizeof(PhysicsHit), At(&l_Hit, &l_Hit.Entity), At(&l_Hit, &l_Hit.Body), At(&l_Hit, &l_Hit.Point), At(&l_Hit, &l_Hit.Normal), At(&l_Hit, &l_Hit.Distance), At(&l_Hit, &l_Hit.Trigger));
            Describe(ref layouts[13], sizeof(PhysicsHit2D), At(&l_Hit2D, &l_Hit2D.Entity), At(&l_Hit2D, &l_Hit2D.Body), At(&l_Hit2D, &l_Hit2D.Point), At(&l_Hit2D, &l_Hit2D.Normal), At(&l_Hit2D, &l_Hit2D.Distance), At(&l_Hit2D, &l_Hit2D.Trigger));

            return c_Count;
        }

        // Times calls from C# into the engine's echo, after a warm-up so the call sites are compiled, and logs the cost of each and what they allocated, which should be nothing. The sum of the echoes, so the loop cannot be optimized away
        [UnmanagedCallersOnly]
        public static long MeasureInterop(int calls)
        {
            calls = Math.Max(calls, 1);
            long l_Sum = 0;
            for (int it_Call = 0; it_Call < 10000; ++it_Call)
            {
                l_Sum += Native.Interop_Echo(it_Call);
            }

            long l_AllocatedBefore = GC.GetAllocatedBytesForCurrentThread();
            long l_Start = Stopwatch.GetTimestamp();
            for (int it_Call = 0; it_Call < calls; ++it_Call)
            {
                l_Sum += Native.Interop_Echo(it_Call);
            }

            TimeSpan l_Elapsed = Stopwatch.GetElapsedTime(l_Start);
            long l_Allocated = GC.GetAllocatedBytesForCurrentThread() - l_AllocatedBefore;

            double l_Nanoseconds = l_Elapsed.TotalNanoseconds / calls;
            NativeLog.Write(l_Allocated > 0 ? LogLevel.Warn : LogLevel.Info, $"Scripting: {calls} calls from C# into the engine took {l_Nanoseconds:F1} ns each and allocated {l_Allocated} B");

            return l_Sum;
        }

        private static int At(void* value, void* field)
        {
            return (int)((byte*)field - (byte*)value);
        }

        private static void Describe(ref TypeLayout layout, int size, params ReadOnlySpan<int> offsets)
        {
            layout.Size = size;
            layout.FieldCount = offsets.Length;
            for (int it_Field = 0; it_Field < offsets.Length && it_Field < TypeLayout.MaxFields; ++it_Field)
            {
                layout.Offsets[it_Field] = offsets[it_Field];
            }
        }
    }
}