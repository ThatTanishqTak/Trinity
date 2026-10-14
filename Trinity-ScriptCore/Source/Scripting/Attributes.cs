using System;

namespace Trinity
{
    // Shows a field that is not public in Properties, where it saves with the scene as a public one does
    [AttributeUsage(AttributeTargets.Field, Inherited = false)]
    public sealed class FieldAttribute : Attribute
    {

    }

    // Keeps a public field out of Properties and the scene, as a field the script keeps for itself
    [AttributeUsage(AttributeTargets.Field, Inherited = false)]
    public sealed class HideAttribute : Attribute
    {

    }
}