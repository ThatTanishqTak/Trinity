namespace Trinity
{
    // A component C# reaches, written by Trinity-InteropGen. Made only by Entity.Get and Entity.Add
    public interface IComponent<TSelf> where TSelf : struct, IComponent<TSelf>
    {
        static abstract int TypeId { get; }
        static abstract string TypeName { get; }
        static abstract TSelf Bind(Entity entity);

        Entity Entity { get; }
    }
}