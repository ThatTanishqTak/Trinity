; Unshipped analyzer release
; https://github.com/dotnet/roslyn-analyzers/blob/main/src/Microsoft.CodeAnalysis.Analyzers/ReleaseTrackingAnalyzers.Help.md

### New Rules

Rule ID | Category | Severity | Notes
--------|----------|----------|-------
TR0001 | Trinity.Scripting | Warning | A field type a script field cannot have
TR0002 | Trinity.Scripting | Error | A generic script class
TR0003 | Trinity.Scripting | Error | A private or protected script class
TR0004 | Trinity.Scripting | Error | A script class without a parameterless constructor
TR0005 | Trinity.Scripting | Warning | A readonly script field
TR0006 | Trinity.Scripting | Warning | A field name that repeats one of a base class
TR0007 | Trinity.Scripting | Warning | A field the generated code cannot reach
TR0008 | Trinity.Scripting | Warning | A list of lists