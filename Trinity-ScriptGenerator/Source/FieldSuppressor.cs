using System.Collections.Immutable;
using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Diagnostics;

namespace Trinity.ScriptGenerator
{
    // A field marked [Field] is written by the engine through the generated code, which the compiler cannot see, so its warnings that the field is never written, never used, or could be readonly are not reported for it
    [DiagnosticAnalyzer(LanguageNames.CSharp)]
    public sealed class FieldSuppressor : DiagnosticSuppressor
    {
        private static readonly SuppressionDescriptor s_NeverAssigned = new("TRS0001", "CS0649", "The engine gives a [Field] its value");
        private static readonly SuppressionDescriptor s_NeverUsed = new("TRS0002", "CS0169", "The engine reads and gives a [Field] its value");
        private static readonly SuppressionDescriptor s_MakeReadOnly = new("TRS0003", "IDE0044", "The engine gives a [Field] its value, so it cannot be readonly");
        private static readonly SuppressionDescriptor s_Unused = new("TRS0004", "IDE0051", "The engine reads and gives a [Field] its value");

        public override ImmutableArray<SuppressionDescriptor> SupportedSuppressions { get; } = ImmutableArray.Create(s_NeverAssigned, s_NeverUsed, s_MakeReadOnly, s_Unused);

        public override void ReportSuppressions(SuppressionAnalysisContext context)
        {
            foreach (Diagnostic it_Diagnostic in context.ReportedDiagnostics)
            {
                SuppressionDescriptor? l_Suppression = SupportedSuppressions.FirstOrDefault(suppression => suppression.SuppressedDiagnosticId == it_Diagnostic.Id);
                SyntaxTree? l_Tree = it_Diagnostic.Location.SourceTree;
                if (l_Suppression == null || l_Tree == null)
                {
                    continue;
                }

                SyntaxNode l_Node = l_Tree.GetRoot(context.CancellationToken).FindNode(it_Diagnostic.Location.SourceSpan);
                ISymbol? l_Symbol = context.GetSemanticModel(l_Tree).GetDeclaredSymbol(l_Node, context.CancellationToken);
                if (l_Symbol is IFieldSymbol l_Field && l_Field.GetAttributes().Any(static attribute => attribute.AttributeClass?.ToDisplayString() == "Trinity.FieldAttribute"))
                {
                    context.ReportSuppression(Suppression.Create(l_Suppression, it_Diagnostic));
                }
            }
        }
    }
}