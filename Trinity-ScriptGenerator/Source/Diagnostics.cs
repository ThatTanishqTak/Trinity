using Microsoft.CodeAnalysis;

namespace Trinity.ScriptGenerator
{
    // What the generator reports, each with an ID of its own that never changes once released
    internal static class Diagnostics
    {
        private const string c_Category = "Trinity.Scripting";

        public static readonly DiagnosticDescriptor UnsupportedFieldType = new("TR0001", "Field type not supported", "{0}.{1} has type {2}, which a script field cannot have, so Properties leaves it out. Mark it [Hide] to keep it out without this warning.", c_Category, DiagnosticSeverity.Warning, true);
        public static readonly DiagnosticDescriptor GenericScript = new("TR0002", "Generic script class", "{0} is generic, and a script class cannot be", c_Category, DiagnosticSeverity.Error, true);
        public static readonly DiagnosticDescriptor UnreachableScript = new("TR0003", "Script class not reachable", "{0} is private or protected, so the engine cannot make it. Make it and the classes it is in internal or public.", c_Category, DiagnosticSeverity.Error, true);
        public static readonly DiagnosticDescriptor NoConstructor = new("TR0004", "No parameterless constructor", "{0} has no constructor without parameters that the engine can call", c_Category, DiagnosticSeverity.Error, true);
        public static readonly DiagnosticDescriptor ReadOnlyField = new("TR0005", "Readonly field", "{0}.{1} is readonly, so it cannot be a script field and Properties leaves it out", c_Category, DiagnosticSeverity.Warning, true);
        public static readonly DiagnosticDescriptor RepeatedField = new("TR0006", "Field name repeats", "{0}.{1} has the name of a field of a class it derives from, so Properties shows only the first", c_Category, DiagnosticSeverity.Warning, true);
        public static readonly DiagnosticDescriptor UnreachableField = new("TR0007", "Field not reachable", "{0}.{1} cannot be reached by the generated code, since it is declared in a generic class or its type is private, so Properties leaves it out", c_Category, DiagnosticSeverity.Warning, true);

        public static DiagnosticDescriptor? Find(string id) => id switch
        {
            "TR0001" => UnsupportedFieldType,
            "TR0002" => GenericScript,
            "TR0003" => UnreachableScript,
            "TR0004" => NoConstructor,
            "TR0005" => ReadOnlyField,
            "TR0006" => RepeatedField,
            "TR0007" => UnreachableField,
            _ => null
        };
    }
}