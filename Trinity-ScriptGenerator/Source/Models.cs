using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;

namespace Trinity.ScriptGenerator
{
    // One named value of an enum field
    internal sealed record EnumValueModel(string Name, long Value);

    // A field the generated code reads and gives values to. Kind names the Trinity.Interop.FieldType, of its elements for a list, TypeName is the field's type in full, AssetMarker is the T of an AssetRef<T>, Accessor is the type that declares a field the generated code cannot reach by name, which an [UnsafeAccessor] reaches instead, and ElementTypeName is a List<T>'s T in full, or null for a field that is not a list
    internal sealed record FieldModel(string Name, string Kind, string TypeName, string? AssetMarker, EquatableArray<EnumValueModel> EnumValues, string? Accessor, string? ElementTypeName);

    // Where a diagnostic points, kept as text and spans rather than as a Location, so models from one run compare equal to the next
    internal sealed record LocationModel(string FilePath, TextSpan Span, LinePositionSpan Lines)
    {
        public static LocationModel? From(Location? location)
        {
            if (location == null || !location.IsInSource)
            {
                return null;
            }

            FileLinePositionSpan l_Lines = location.GetLineSpan();

            return new LocationModel(l_Lines.Path, location.SourceSpan, l_Lines.Span);
        }

        public Location ToLocation() => Location.Create(FilePath, Span, Lines);
    }

    internal sealed record DiagnosticModel(string Id, LocationModel? Location, EquatableArray<string> Arguments);

    // A script class: its full name as the engine knows it, its type in full, the callbacks it overrides, its fields base class first, and whether it can be registered at all
    internal sealed record ScriptModel(string Name, string TypeName, EquatableArray<string> Callbacks, EquatableArray<FieldModel> Fields, EquatableArray<DiagnosticModel> Diagnostics, bool Valid);
}