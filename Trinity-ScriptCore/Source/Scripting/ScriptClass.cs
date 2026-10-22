using System;
using System.Collections.Generic;
using System.ComponentModel;
using Trinity.Interop;

namespace Trinity.Scripting
{
    // One named value of an enum field, in the order the enum declares them
    [EditorBrowsable(EditorBrowsableState.Never)]
    public readonly struct ScriptEnumValue
    {
        public string Name { get; }
        public long Value { get; }

        public ScriptEnumValue(string name, long value)
        {
            Name = name;
            Value = value;
        }
    }

    // A field a script class shows in Properties: its name and type, the type of its elements for a list or its own type for any other, and for an asset reference, or a list of them, the asset type it takes, or for an enum its values
    [EditorBrowsable(EditorBrowsableState.Never)]
    public sealed class ScriptField
    {
        public string Name { get; }
        public FieldType Type { get; }
        public FieldType ElementType { get; }
        public string AssetType { get; }
        public IReadOnlyList<ScriptEnumValue> EnumValues { get; }

        public ScriptField(string name, FieldType type, string? assetType = null, ScriptEnumValue[]? enumValues = null, FieldType? elementType = null)
        {
            Name = name;
            Type = type;
            ElementType = elementType ?? type;
            AssetType = assetType ?? string.Empty;
            EnumValues = enumValues ?? Array.Empty<ScriptEnumValue>();
        }
    }

    // A script class as the source generator describes it: its full name, the callbacks it overrides, and its fields in order, with how to make an instance and how each field is read and given a value, all without reflection
    [EditorBrowsable(EditorBrowsableState.Never)]
    public abstract class ScriptClass
    {
        public string Name { get; }
        public ScriptCallbacks Callbacks { get; }
        public IReadOnlyList<ScriptField> Fields { get; }

        protected ScriptClass(string name, ScriptCallbacks callbacks, ScriptField[] fields)
        {
            Name = name;
            Callbacks = callbacks;
            Fields = fields;
        }

        public abstract Script Create();
        public abstract void SetField(Script script, int index, in FieldValue value);
        public abstract FieldValue GetField(Script script, int index);

        // An element of a list field, given and read after the list, which a class without list fields has none of
        public virtual void SetElement(Script script, int index, int element, in FieldValue value) => throw new ArgumentOutOfRangeException(nameof(index));
        public virtual FieldValue GetElement(Script script, int index, int element) => throw new ArgumentOutOfRangeException(nameof(index));
    }
}