using System.Runtime.InteropServices;

namespace Trinity
{
    // Laid out as glm::quat, which keeps W last, and which the engine checks at start. The identity rotation is (0, 0, 0, 1)
    [StructLayout(LayoutKind.Sequential)]
    public struct Quaternion
    {
        public float X;
        public float Y;
        public float Z;
        public float W;

        public static Quaternion Identity => new Quaternion(0.0f, 0.0f, 0.0f, 1.0f);

        public Quaternion(float x, float y, float z, float w)
        {
            X = x;
            Y = y;
            Z = z;
            W = w;
        }
    }
}