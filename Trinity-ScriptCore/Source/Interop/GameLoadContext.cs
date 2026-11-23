using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

namespace Trinity.Interop
{
    // The collectible context a game assembly loads into. ScriptCore comes from the default context
    internal sealed class GameLoadContext : AssemblyLoadContext
    {
        private readonly AssemblyDependencyResolver m_Resolver;

        // What the game depends on is found through deps.json, beside the build the copy was made from
        public GameLoadContext(string buildPath) : base("Trinity game", isCollectible: true)
        {
            m_Resolver = new AssemblyDependencyResolver(buildPath);
        }

        // The game's assembly, from a copy with its PDB beside it, so the build can write over its files
        [UnconditionalSuppressMessage("Trimming", "IL2026", Justification = "A game assembly loads at runtime only in builds that ship it beside an untrimmed engine")]
        public Assembly LoadMain(string path)
        {
            return LoadFromAssemblyPath(Path.GetFullPath(path));
        }

        // Trimming cannot see what a game loaded at runtime uses. A NativeAOT build compiles it in instead
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