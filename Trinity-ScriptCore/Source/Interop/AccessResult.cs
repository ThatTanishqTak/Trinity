namespace Trinity.Interop
{
    // What a component's or an entity's function in the engine tells C#, numbered as the engine's Interop::AccessResult numbers it
    internal enum AccessResult
    {
        Done,
        NoEntity,
        NoComponent,
        BadValue
    }
}