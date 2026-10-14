namespace Trinity
{
    // A component C# reaches, which Trinity-InteropGen writes from the engine's ScriptComponents.def: a handle to one entity's, numbered as the engine numbers it, made only by Entity.Get and Entity.Add
    public interface IComponent<TSelf> where TSelf : struct, IComponent<TSelf>
    {
        static abstract int TypeId { get; }
        static abstract string TypeName { get; }
        static abstract TSelf Bind(Entity entity);

        Entity Entity { get; }
    }
}