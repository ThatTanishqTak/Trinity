using System.Runtime.InteropServices;

namespace Trinity
{
    // A linear RGBA colour, laid out as the glm::vec4 the engine keeps one in, which it checks at start
    [StructLayout(LayoutKind.Sequential)]
    public struct Color
    {
        public float R;
        public float G;
        public float B;
        public float A;

        public static Color White => new Color(1.0f, 1.0f, 1.0f, 1.0f);

        public Color(float r, float g, float b, float a = 1.0f)
        {
            R = r;
            G = g;
            B = b;
            A = a;
        }
    }
}