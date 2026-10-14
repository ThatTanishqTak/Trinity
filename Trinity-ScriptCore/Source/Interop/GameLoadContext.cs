using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

namespace Trinity.Interop
{
    // The load context a game assembly and what it depends on load into, which unloads with them. ScriptCore and the framework come from the default context, so the game shares the engine's one copy of them. Each assembly is read from its bytes, with its PDB beside it for stack traces with lines, so a build can write over the files while they run
    internal sealed class GameLoadContext : AssemblyLoadContext
    {
        private readonly AssemblyDependencyResolver m_Resolver;

        public GameLoadContext(string assemblyPath) : base("Trinity game", isCollectible: true)
        {
            m_Resolver = new AssemblyDependencyResolver(assemblyPath);
        }

        // Trimming cannot see what a game loaded at runtime uses, so the engine is never trimmed against one. A NativeAOT build compiles the game in instead
        [UnconditionalSuppressMessage("Trimming", "IL2026", Justification = "A game assembly loads at runtime only in builds that ship it beside an untrimmed engine")]
        public Assembly LoadFile(string path)
        {
            using MemoryStream l_Assembly = new(File.ReadAllBytes(path));
            string l_PdbPath = Path.ChangeExtension(path, ".pdb");
            using MemoryStream? l_Pdb = File.Exists(l_PdbPath) ? new MemoryStream(File.ReadAllBytes(l_PdbPath)) : null;

            return LoadFromStream(l_Assembly, l_Pdb);
        }

        protected override Assembly? Load(AssemblyName assemblyName)
        {
            if (assemblyName.Name == typeof(GameLoadContext).Assembly.GetName().Name)
            {
                return null;
            }

            string? l_Path = m_Resolver.ResolveAssemblyToPath(assemblyName);

            return l_Path != null ? LoadFile(l_Path) : null;
        }
    }
}