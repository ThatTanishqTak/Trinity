using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;

namespace Trinity.ScriptGenerator
{
    // One named value of an enum field
    internal sealed record EnumValueModel(string Name, long Value);

    // A field the generated code reads and writes. Accessor is for one it cannot reach by name
    internal sealed record FieldModel(string Name, string Kind, string TypeName, string? AssetMarker, EquatableArray<EnumValueModel> EnumValues, string? Accessor, string? ElementTypeName);

    // Where a diagnostic points, kept as text and spans, so models from one run equal the next
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

    // Its full name as the engine knows it, its type, its callbacks, and its fields base class first
    internal sealed record ScriptModel(string Name, string TypeName, EquatableArray<string> Callbacks, EquatableArray<FieldModel> Fields, EquatableArray<DiagnosticModel> Diagnostics, bool Valid);
}