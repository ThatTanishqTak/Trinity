namespace Trinity.Interop
{
    // Numbered as the engine's ScriptFieldType, which it checks at build time. A list holds no lists
    public enum FieldType : int
    {
        Bool,
        Int32,
        Int64,
        UInt32,
        UInt64,
        Float,
        Double,
        Vector2,
        Vector3,
        Vector4,
        Quaternion,
        Color,
        String,
        Enum,
        Entity,
        Asset,
        InputAction,
        List
    }
}