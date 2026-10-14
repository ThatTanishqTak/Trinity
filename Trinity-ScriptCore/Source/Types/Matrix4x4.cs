using System.Runtime.InteropServices;

namespace Trinity
{
    // Laid out as glm::mat4, column after column, so Column3 holds the translation, which the engine checks at start
    [StructLayout(LayoutKind.Sequential)]
    public struct Matrix4x4
    {
        public Vector4 Column0;
        public Vector4 Column1;
        public Vector4 Column2;
        public Vector4 Column3;

        public static Matrix4x4 Identity => new Matrix4x4(new Vector4(1.0f, 0.0f, 0.0f, 0.0f), new Vector4(0.0f, 1.0f, 0.0f, 0.0f), new Vector4(0.0f, 0.0f, 1.0f, 0.0f), new Vector4(0.0f, 0.0f, 0.0f, 1.0f));

        public Matrix4x4(Vector4 column0, Vector4 column1, Vector4 column2, Vector4 column3)
        {
            Column0 = column0;
            Column1 = column1;
            Column2 = column2;
            Column3 = column3;
        }
    }
}