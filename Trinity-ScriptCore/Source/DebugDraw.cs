using Trinity.Interop;

namespace Trinity
{
    // Lines in world space for seeing what a script is doing, drawn this frame into every view that shows debug lines, then cleared, as the engine's DebugDraw draws them. Colours are sRGB with alpha, as they show. OnTop shows a line over everything, rather than behind the scene's meshes. Nothing is drawn in Distribution
    public static unsafe class DebugDraw
    {
        public static void Line(Vector3 from, Vector3 to, Color color, bool onTop = false) => Native.DebugDraw_Line(&from, &to, &color, onTop);

        // The box between -1 and 1 on each axis under a transform
        public static void Box(Matrix4x4 transform, Color color, bool onTop = false) => Native.DebugDraw_Box(&transform, &color, onTop);

        // An axis-aligned box, by its centre and half extents
        public static void Box(Vector3 center, Vector3 halfExtents, Color color, bool onTop = false) => Native.DebugDraw_AxisBox(&center, &halfExtents, &color, onTop);

        public static void Circle(Vector3 center, Vector3 normal, float radius, Color color, bool onTop = false) => Native.DebugDraw_Circle(&center, &normal, radius, &color, onTop);

        // A circle about each axis
        public static void Sphere(Vector3 center, float radius, Color color, bool onTop = false) => Native.DebugDraw_Sphere(&center, radius, &color, onTop);

        // The points within the radius of the segment between the two centres
        public static void Capsule(Vector3 start, Vector3 end, float radius, Color color, bool onTop = false) => Native.DebugDraw_Capsule(&start, &end, radius, &color, onTop);

        // A line with four barbs at its head, a fifth of its length
        public static void Arrow(Vector3 from, Vector3 to, Color color, bool onTop = false) => Native.DebugDraw_Arrow(&from, &to, &color, onTop);

        // Three lines along the axes through a point, each the size long
        public static void Cross(Vector3 center, float size, Color color, bool onTop = false) => Native.DebugDraw_Cross(&center, size, &color, onTop);
    }
}