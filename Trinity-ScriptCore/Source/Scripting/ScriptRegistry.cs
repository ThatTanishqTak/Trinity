using System.Collections.Generic;
using System.ComponentModel;

namespace Trinity.Scripting
{
    // Where a game assembly's generated module initializer adds its script classes, which ScriptCore takes as it loads the assembly
    [EditorBrowsable(EditorBrowsableState.Never)]
    public static class ScriptRegistry
    {
        private static List<ScriptClass>? s_Collected;

        public static void Add(ScriptClass scriptClass)
        {
            (s_Collected ??= new List<ScriptClass>()).Add(scriptClass);
        }

        // Every class added since the last call
        internal static List<ScriptClass> Take()
        {
            List<ScriptClass> l_Classes = s_Collected ?? new List<ScriptClass>();
            s_Collected = null;

            return l_Classes;
        }
    }
}