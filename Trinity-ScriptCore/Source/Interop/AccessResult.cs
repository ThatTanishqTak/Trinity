namespace Trinity.Interop
{
    // What an engine function tells C#, numbered as the engine's Interop::AccessResult
    internal enum AccessResult
    {
        Done,
        NoEntity,
        NoComponent,
        BadValue
    }
}