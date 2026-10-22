namespace Trinity.Interop
{
    // What a script field holds, numbered as the engine's ScriptFieldType numbers it, which it checks at build time. A list's elements are of any type but a list
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