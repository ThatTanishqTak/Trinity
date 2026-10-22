using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Text;
using System.Threading;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Trinity.ScriptGenerator
{
    // Finds every class of the project that derives from Trinity.Script and writes, into one file, a Trinity.Scripting.ScriptClass for each: its name, the callbacks it overrides, its fields, how to make it, and how each field is read and given a value. A module initializer adds them all to ScriptCore as the assembly loads. A field is a public instance field without [Hide], or any instance field with [Field], and one the generated code cannot reach by name is reached through [UnsafeAccessor], which NativeAOT keeps
    [Generator(LanguageNames.CSharp)]
    public sealed class ScriptGenerator : IIncrementalGenerator
    {
        private const string c_ScriptType = "Trinity.Script";

        // Each callback Trinity.Script has, with the Trinity.Scripting.ScriptCallbacks flag that says a class overrides it
        private static readonly (string Method, string Flag)[] s_Callbacks =
        {
            ("OnCreate", "Create"),
            ("OnFixedUpdate", "FixedUpdate"),
            ("OnUpdate", "Update"),
            ("OnLateUpdate", "LateUpdate"),
            ("OnDestroy", "Destroy"),
            ("OnReloaded", "Reloaded"),
            ("OnContactBegin", "Physics"),
            ("OnContactStay", "Physics"),
            ("OnContactEnd", "Physics"),
            ("OnTriggerEnter", "Physics"),
            ("OnTriggerExit", "Physics"),
            ("OnJointBreak", "Physics"),
            ("OnCharacterHit", "Physics")
        };

        private static readonly SymbolDisplayFormat s_EngineName = new(typeQualificationStyle: SymbolDisplayTypeQualificationStyle.NameAndContainingTypesAndNamespaces);

        public void Initialize(IncrementalGeneratorInitializationContext context)
        {
            IncrementalValueProvider<ImmutableArray<ScriptModel?>> l_Scripts = context.SyntaxProvider
                .CreateSyntaxProvider(static (node, _) => node is ClassDeclarationSyntax l_Class && l_Class.BaseList != null, static (syntax, cancel) => Analyze(syntax, cancel))
                .Where(static model => model != null)
                .Collect();

            context.RegisterSourceOutput(l_Scripts, static (output, scripts) => Emit(output, scripts));
        }

        private static ScriptModel? Analyze(GeneratorSyntaxContext syntax, CancellationToken cancel)
        {
            if (syntax.SemanticModel.GetDeclaredSymbol(syntax.Node, cancel) is not INamedTypeSymbol l_Class || l_Class.IsAbstract || l_Class.IsStatic)
            {
                return null;
            }

            INamedTypeSymbol? l_Script = syntax.SemanticModel.Compilation.GetTypeByMetadataName(c_ScriptType);
            if (l_Script == null || !DerivesFrom(l_Class, l_Script))
            {
                return null;
            }

            List<DiagnosticModel> l_Diagnostics = new();
            string l_Name = l_Class.ToDisplayString(s_EngineName);
            LocationModel? l_Location = LocationModel.From(l_Class.Locations.FirstOrDefault());
            bool l_Valid = true;
            if (IsGeneric(l_Class))
            {
                l_Diagnostics.Add(new DiagnosticModel(Diagnostics.GenericScript.Id, l_Location, Strings(l_Name)));
                l_Valid = false;
            }

            if (!IsReachable(l_Class))
            {
                l_Diagnostics.Add(new DiagnosticModel(Diagnostics.UnreachableScript.Id, l_Location, Strings(l_Name)));
                l_Valid = false;
            }

            if (!l_Class.InstanceConstructors.Any(static constructor => constructor.Parameters.Length == 0 && constructor.DeclaredAccessibility is not (Accessibility.Private or Accessibility.Protected or Accessibility.ProtectedAndInternal)))
            {
                l_Diagnostics.Add(new DiagnosticModel(Diagnostics.NoConstructor.Id, l_Location, Strings(l_Name)));
                l_Valid = false;
            }

            // The classes between this one and Trinity.Script, base first, so fields are in the order they are declared down the hierarchy
            List<INamedTypeSymbol> l_Chain = new();
            for (INamedTypeSymbol? it_Type = l_Class; it_Type != null && !SymbolEqualityComparer.Default.Equals(it_Type, l_Script); it_Type = it_Type.BaseType)
            {
                l_Chain.Insert(0, it_Type);
            }

            SortedSet<string> l_Callbacks = new(StringComparer.Ordinal);
            List<FieldModel> l_Fields = new();
            foreach (INamedTypeSymbol it_Type in l_Chain)
            {
                cancel.ThrowIfCancellationRequested();
                foreach (ISymbol it_Member in it_Type.GetMembers())
                {
                    if (it_Member is IMethodSymbol { IsOverride: true } l_Method)
                    {
                        foreach ((string it_Method, string it_Flag) in s_Callbacks)
                        {
                            if (l_Method.Name == it_Method)
                            {
                                l_Callbacks.Add(it_Flag);
                            }
                        }
                    }
                    else if (it_Member is IFieldSymbol l_Field && Describe(l_Class, l_Field, l_Fields, l_Diagnostics) is FieldModel l_Model)
                    {
                        l_Fields.Add(l_Model);
                    }
                }
            }

            return new ScriptModel(l_Name, l_Class.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat), new EquatableArray<string>(l_Callbacks.ToArray()), new EquatableArray<FieldModel>(l_Fields.ToArray()), new EquatableArray<DiagnosticModel>(l_Diagnostics.ToArray()), l_Valid);
        }

        // Null for a member that is not a script field, with a diagnostic when it looks like one but cannot be
        private static FieldModel? Describe(INamedTypeSymbol script, IFieldSymbol field, List<FieldModel> fields, List<DiagnosticModel> diagnostics)
        {
            if (field.IsStatic || field.IsConst || field.IsImplicitlyDeclared)
            {
                return null;
            }

            bool l_Marked = HasAttribute(field, "Trinity.FieldAttribute");
            bool l_Hidden = HasAttribute(field, "Trinity.HideAttribute");
            if (l_Hidden || (field.DeclaredAccessibility != Accessibility.Public && !l_Marked))
            {
                return null;
            }

            // Named by the class that declares the field, so a base class's field is reported once for all the scripts that derive from it
            string l_ScriptName = field.ContainingType.Name;
            LocationModel? l_Location = LocationModel.From(field.Locations.FirstOrDefault());
            if (field.IsReadOnly)
            {
                diagnostics.Add(new DiagnosticModel(Diagnostics.ReadOnlyField.Id, l_Location, Strings(l_ScriptName, field.Name)));

                return null;
            }

            if (fields.Any(existing => existing.Name == field.Name))
            {
                diagnostics.Add(new DiagnosticModel(Diagnostics.RepeatedField.Id, l_Location, Strings(l_ScriptName, field.Name)));

                return null;
            }

            ITypeSymbol l_Type = field.Type;
            ITypeSymbol? l_Element = GetListElement(l_Type);
            if (l_Element != null && GetListElement(l_Element) != null)
            {
                diagnostics.Add(new DiagnosticModel(Diagnostics.NestedList.Id, l_Location, Strings(l_ScriptName, field.Name, l_Type.ToDisplayString())));

                return null;
            }

            // A list is what it holds, any number of them
            ITypeSymbol l_Held = l_Element ?? l_Type;
            (string? l_Kind, string? l_Marker) = Classify(l_Held);
            if (l_Kind == null)
            {
                diagnostics.Add(new DiagnosticModel(Diagnostics.UnsupportedFieldType.Id, l_Location, Strings(l_ScriptName, field.Name, l_Type.ToDisplayString())));

                return null;
            }

            // A field the generated code names directly is public or internal. Others go through an accessor, which a generic class cannot have, and the field's type must be one the generated code can name either way
            bool l_Direct = field.DeclaredAccessibility is Accessibility.Public or Accessibility.Internal or Accessibility.ProtectedOrInternal;
            if ((!l_Direct && IsGeneric(field.ContainingType)) || !IsReachable(l_Type))
            {
                diagnostics.Add(new DiagnosticModel(Diagnostics.UnreachableField.Id, l_Location, Strings(l_ScriptName, field.Name)));

                return null;
            }

            EnumValueModel[] l_Values = Array.Empty<EnumValueModel>();
            if (l_Kind == "Enum")
            {
                l_Values = l_Held.GetMembers().OfType<IFieldSymbol>().Where(static member => member.HasConstantValue).Select(static member => new EnumValueModel(member.Name, ToInt64(member.ConstantValue))).ToArray();
            }

            return new FieldModel(field.Name, l_Kind, l_Type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat), l_Marker, new EquatableArray<EnumValueModel>(l_Values), l_Direct ? null : field.ContainingType.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat), l_Element?.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat));
        }

        // What a List<T> holds, or nothing for any other type
        private static ITypeSymbol? GetListElement(ITypeSymbol type) => type is INamedTypeSymbol { TypeArguments.Length: 1 } l_Named && l_Named.OriginalDefinition.ToDisplayString(s_EngineName) == "System.Collections.Generic.List" ? l_Named.TypeArguments[0] : null;

        // The Trinity.Interop.FieldType a type is, and for an AssetRef<T> its T, or nothing for a type a field cannot have
        private static (string? Kind, string? Marker) Classify(ITypeSymbol type)
        {
            switch (type.SpecialType)
            {
                case SpecialType.System_Boolean: return ("Bool", null);
                case SpecialType.System_Int32: return ("Int32", null);
                case SpecialType.System_Int64: return ("Int64", null);
                case SpecialType.System_UInt32: return ("UInt32", null);
                case SpecialType.System_UInt64: return ("UInt64", null);
                case SpecialType.System_Single: return ("Float", null);
                case SpecialType.System_Double: return ("Double", null);
                case SpecialType.System_String: return ("String", null);
            }

            if (type.TypeKind == TypeKind.Enum)
            {
                return ("Enum", null);
            }

            if (type is not INamedTypeSymbol l_Named)
            {
                return (null, null);
            }

            string l_Name = l_Named.OriginalDefinition.ToDisplayString(s_EngineName);
            switch (l_Name)
            {
                case "Trinity.Vector2": return ("Vector2", null);
                case "Trinity.Vector3": return ("Vector3", null);
                case "Trinity.Vector4": return ("Vector4", null);
                case "Trinity.Quaternion": return ("Quaternion", null);
                case "Trinity.Color": return ("Color", null);
                case "Trinity.Entity": return ("Entity", null);
                case "Trinity.InputAction": return ("InputAction", null);
            }

            if (l_Name == "Trinity.AssetRef" && l_Named.TypeArguments.Length == 1 && l_Named.TypeArguments[0] is INamedTypeSymbol l_Marker)
            {
                return ("Asset", l_Marker.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat));
            }

            return (null, null);
        }

        private static void Emit(SourceProductionContext output, ImmutableArray<ScriptModel?> scripts)
        {
            // A partial class is found once for each part with a base list
            List<ScriptModel> l_Scripts = new();
            HashSet<string> l_Seen = new(StringComparer.Ordinal);
            foreach (ScriptModel? it_Script in scripts)
            {
                if (it_Script != null && l_Seen.Add(it_Script.TypeName))
                {
                    l_Scripts.Add(it_Script);
                }
            }

            l_Scripts.Sort(static (left, right) => string.CompareOrdinal(left.Name, right.Name));
            HashSet<DiagnosticModel> l_Reported = new();
            foreach (ScriptModel it_Script in l_Scripts)
            {
                foreach (DiagnosticModel it_Diagnostic in it_Script.Diagnostics)
                {
                    if (l_Reported.Add(it_Diagnostic) && Diagnostics.Find(it_Diagnostic.Id) is DiagnosticDescriptor l_Descriptor)
                    {
                        output.ReportDiagnostic(Diagnostic.Create(l_Descriptor, it_Diagnostic.Location?.ToLocation(), it_Diagnostic.Arguments.ToArray<object>()));
                    }
                }
            }

            List<ScriptModel> l_Valid = l_Scripts.Where(static script => script.Valid).ToList();
            if (l_Valid.Count == 0)
            {
                return;
            }

            StringBuilder l_Text = new();
            l_Text.AppendLine("// <auto-generated/>");
            l_Text.AppendLine("// Written by Trinity-ScriptGenerator as the project builds, from its classes that derive from Trinity.Script");
            l_Text.AppendLine("#nullable disable");
            l_Text.AppendLine("#pragma warning disable");
            l_Text.AppendLine();
            l_Text.AppendLine("namespace Trinity.Generated");
            l_Text.AppendLine("{");
            for (int it_Index = 0; it_Index < l_Valid.Count; ++it_Index)
            {
                WriteClass(l_Text, l_Valid[it_Index], it_Index);
            }

            l_Text.AppendLine("    file static class ScriptClasses");
            l_Text.AppendLine("    {");
            l_Text.AppendLine("        [global::System.Runtime.CompilerServices.ModuleInitializer]");
            l_Text.AppendLine("        internal static void Register()");
            l_Text.AppendLine("        {");
            for (int it_Index = 0; it_Index < l_Valid.Count; ++it_Index)
            {
                l_Text.AppendLine($"            global::Trinity.Scripting.ScriptRegistry.Add(new ScriptClass{it_Index}());");
            }

            l_Text.AppendLine("        }");
            l_Text.AppendLine("    }");
            l_Text.AppendLine("}");

            output.AddSource("TrinityScripts.g.cs", l_Text.ToString());
        }

        private static void WriteClass(StringBuilder text, ScriptModel script, int index)
        {
            string l_Callbacks = script.Callbacks.Count == 0 ? "global::Trinity.Scripting.ScriptCallbacks.None" : string.Join(" | ", script.Callbacks.Select(static flag => $"global::Trinity.Scripting.ScriptCallbacks.{flag}"));
            text.AppendLine($"    file sealed class ScriptClass{index} : global::Trinity.Scripting.ScriptClass");
            text.AppendLine("    {");
            text.AppendLine($"        public ScriptClass{index}() : base({Literal(script.Name)}, {l_Callbacks}, new global::Trinity.Scripting.ScriptField[]");
            text.AppendLine("        {");
            foreach (FieldModel it_Field in script.Fields)
            {
                text.Append($"            new global::Trinity.Scripting.ScriptField({Literal(it_Field.Name)}, global::Trinity.Interop.FieldType.{(it_Field.ElementTypeName != null ? "List" : it_Field.Kind)}");
                if (it_Field.AssetMarker != null)
                {
                    text.Append($", assetType: {it_Field.AssetMarker}.AssetType");
                }

                if (it_Field.EnumValues.Count != 0)
                {
                    text.Append(", enumValues: new global::Trinity.Scripting.ScriptEnumValue[] { ");
                    text.Append(string.Join(", ", it_Field.EnumValues.Select(static value => $"new({Literal(value.Name)}, {value.Value}L)")));
                    text.Append(" }");
                }

                if (it_Field.ElementTypeName != null)
                {
                    text.Append($", elementType: global::Trinity.Interop.FieldType.{it_Field.Kind}");
                }

                text.AppendLine("),");
            }

            text.AppendLine("        })");
            text.AppendLine("        {");
            text.AppendLine("        }");
            text.AppendLine();
            text.AppendLine($"        public override global::Trinity.Script Create() => new {script.TypeName}();");
            text.AppendLine();
            text.AppendLine("        public override void SetField(global::Trinity.Script script, int index, in global::Trinity.Interop.FieldValue value)");
            text.AppendLine("        {");
            text.AppendLine($"            {script.TypeName} l_Target = ({script.TypeName})script;");
            text.AppendLine("            switch (index)");
            text.AppendLine("            {");
            for (int it_Field = 0; it_Field < script.Fields.Count; ++it_Field)
            {
                FieldModel l_Field = script.Fields[it_Field];
                string l_Value = l_Field.ElementTypeName != null ? $"value.ToList<{l_Field.ElementTypeName}>()" : Read(l_Field.Kind, l_Field.TypeName);
                text.AppendLine($"                case {it_Field}: {Target(l_Field, it_Field)} = {l_Value}; break;");
            }

            text.AppendLine("            }");
            text.AppendLine("        }");
            text.AppendLine();
            text.AppendLine("        public override global::Trinity.Interop.FieldValue GetField(global::Trinity.Script script, int index)");
            text.AppendLine("        {");
            text.AppendLine($"            {script.TypeName} l_Target = ({script.TypeName})script;");
            text.AppendLine("            switch (index)");
            text.AppendLine("            {");
            for (int it_Field = 0; it_Field < script.Fields.Count; ++it_Field)
            {
                FieldModel l_Field = script.Fields[it_Field];
                string l_Value = l_Field.ElementTypeName != null ? $"global::Trinity.Interop.FieldValue.List(global::Trinity.Interop.FieldType.{l_Field.Kind}, {Target(l_Field, it_Field)}?.Count ?? 0)" : Make(l_Field.Kind, Target(l_Field, it_Field));
                text.AppendLine($"                case {it_Field}: return {l_Value};");
            }

            text.AppendLine("                default: throw new global::System.ArgumentOutOfRangeException(nameof(index));");
            text.AppendLine("            }");
            text.AppendLine("        }");
            if (script.Fields.Any(static field => field.ElementTypeName != null))
            {
                WriteElements(text, script);
            }

            for (int it_Field = 0; it_Field < script.Fields.Count; ++it_Field)
            {
                FieldModel l_Field = script.Fields[it_Field];
                if (l_Field.Accessor != null)
                {
                    text.AppendLine();
                    text.AppendLine($"        [global::System.Runtime.CompilerServices.UnsafeAccessor(global::System.Runtime.CompilerServices.UnsafeAccessorKind.Field, Name = {Literal(l_Field.Name)})]");
                    text.AppendLine($"        private static extern ref {l_Field.TypeName} Field{it_Field}({l_Field.Accessor} target);");
                }
            }

            text.AppendLine("    }");
            text.AppendLine();
        }

        // A list field's elements by their index, given into the new list the field was given, and read from the list it has
        private static void WriteElements(StringBuilder text, ScriptModel script)
        {
            text.AppendLine();
            text.AppendLine("        public override void SetElement(global::Trinity.Script script, int index, int element, in global::Trinity.Interop.FieldValue value)");
            text.AppendLine("        {");
            text.AppendLine($"            {script.TypeName} l_Target = ({script.TypeName})script;");
            text.AppendLine("            switch (index)");
            text.AppendLine("            {");
            for (int it_Field = 0; it_Field < script.Fields.Count; ++it_Field)
            {
                FieldModel l_Field = script.Fields[it_Field];
                if (l_Field.ElementTypeName != null)
                {
                    text.AppendLine($"                case {it_Field}: {Target(l_Field, it_Field)}[element] = {Read(l_Field.Kind, l_Field.ElementTypeName)}; break;");
                }
            }

            text.AppendLine("                default: throw new global::System.ArgumentOutOfRangeException(nameof(index));");
            text.AppendLine("            }");
            text.AppendLine("        }");
            text.AppendLine();
            text.AppendLine("        public override global::Trinity.Interop.FieldValue GetElement(global::Trinity.Script script, int index, int element)");
            text.AppendLine("        {");
            text.AppendLine($"            {script.TypeName} l_Target = ({script.TypeName})script;");
            text.AppendLine("            switch (index)");
            text.AppendLine("            {");
            for (int it_Field = 0; it_Field < script.Fields.Count; ++it_Field)
            {
                FieldModel l_Field = script.Fields[it_Field];
                if (l_Field.ElementTypeName != null)
                {
                    text.AppendLine($"                case {it_Field}: return {Make(l_Field.Kind, $"{Target(l_Field, it_Field)}[element]")};");
                }
            }

            text.AppendLine("                default: throw new global::System.ArgumentOutOfRangeException(nameof(index));");
            text.AppendLine("            }");
            text.AppendLine("        }");
        }

        // The field as an expression that can be read and written
        private static string Target(FieldModel field, int index) => field.Accessor != null ? $"Field{index}(l_Target)" : $"l_Target.@{field.Name}";

        // A value of the kind, of the type in full, from the FieldValue named value
        private static string Read(string kind, string typeName) => kind switch
        {
            "Bool" => "value.ToBool()",
            "Int32" => "value.ToInt32()",
            "Int64" => "value.ToInt64()",
            "UInt32" => "value.ToUInt32()",
            "UInt64" => "value.ToUInt64()",
            "Float" => "value.ToFloat()",
            "Double" => "value.ToDouble()",
            "Vector2" => "value.ToVector2()",
            "Vector3" => "value.ToVector3()",
            "Vector4" => "value.ToVector4()",
            "Quaternion" => "value.ToQuaternion()",
            "Color" => "value.ToColor()",
            "String" => "value.ToText()",
            "Enum" => $"unchecked(({typeName})value.ToEnum())",
            "Entity" => "value.ToEntity()",
            "Asset" => $"new {typeName}(value.ToAsset())",
            "InputAction" => "value.ToInputAction()",
            _ => "default"
        };

        private static string Make(string kind, string target) => kind switch
        {
            "Enum" => $"global::Trinity.Interop.FieldValue.Enum(unchecked((long){target}))",
            "Asset" => $"global::Trinity.Interop.FieldValue.Asset({target}.ID)",
            _ => $"global::Trinity.Interop.FieldValue.{kind}({target})"
        };

        private static bool DerivesFrom(INamedTypeSymbol type, INamedTypeSymbol baseType)
        {
            for (INamedTypeSymbol? it_Type = type.BaseType; it_Type != null; it_Type = it_Type.BaseType)
            {
                if (SymbolEqualityComparer.Default.Equals(it_Type, baseType))
                {
                    return true;
                }
            }

            return false;
        }

        private static bool IsGeneric(INamedTypeSymbol? type)
        {
            for (INamedTypeSymbol? it_Type = type; it_Type != null; it_Type = it_Type.ContainingType)
            {
                if (it_Type.IsGenericType)
                {
                    return true;
                }
            }

            return false;
        }

        // Whether code elsewhere in the assembly can name the type: neither it nor a type it is nested in is private or protected
        private static bool IsReachable(ITypeSymbol type)
        {
            if (type is INamedTypeSymbol { IsGenericType: true } l_Generic && l_Generic.TypeArguments.Any(static argument => !IsReachable(argument)))
            {
                return false;
            }

            for (ITypeSymbol? it_Type = type; it_Type != null; it_Type = it_Type.ContainingType)
            {
                if (it_Type.DeclaredAccessibility is Accessibility.Private or Accessibility.Protected or Accessibility.ProtectedAndInternal)
                {
                    return false;
                }
            }

            return true;
        }

        private static bool HasAttribute(ISymbol symbol, string name) => symbol.GetAttributes().Any(attribute => attribute.AttributeClass?.ToDisplayString(s_EngineName) == name);

        private static long ToInt64(object? value) => value switch
        {
            ulong l_Unsigned => unchecked((long)l_Unsigned),
            null => 0,
            _ => Convert.ToInt64(value, System.Globalization.CultureInfo.InvariantCulture)
        };

        private static string Literal(string text) => SymbolDisplay.FormatLiteral(text, true);

        private static EquatableArray<string> Strings(params string[] values) => new(values);
    }
}