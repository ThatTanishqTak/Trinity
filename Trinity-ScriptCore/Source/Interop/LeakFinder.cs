using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Runtime.Loader;

namespace Trinity.Interop
{
    // What keeps an unloaded game context alive, found by reading every static field of every assembly outside it, ScriptCore's and the framework's, and the fields of the object each holds, as AppDomain.CurrentDomain holds its events: a delegate or an event that calls into the game, an object of one of the game's types, or a collection that holds either. Only run once a context has not been collected, since reading a static field runs its type's static constructor
    internal static class LeakFinder
    {
        private const int c_MaxFindings = 8;
        private const int c_MaxItems = 256;

        [UnconditionalSuppressMessage("Trimming", "IL2026", Justification = "Run in Forge only, after a reload whose context was not collected, over whatever metadata the loaded assemblies have")]
        [UnconditionalSuppressMessage("Trimming", "IL2075", Justification = "Run in Forge only, after a reload whose context was not collected, over whatever metadata the loaded assemblies have")]
        [UnconditionalSuppressMessage("Trimming", "IL2065", Justification = "Run in Forge only, after a reload whose context was not collected, over whatever metadata the loaded assemblies have")]
        public static string Describe(WeakReference context)
        {
            if (context.Target is not AssemblyLoadContext l_Context)
            {
                return "It has gone since.";
            }

            List<string> l_Findings = new();
            foreach (Assembly it_Assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                if (AssemblyLoadContext.GetLoadContext(it_Assembly) == l_Context)
                {
                    continue;
                }

                foreach (Type? it_Type in GetTypes(it_Assembly))
                {
                    if (it_Type == null || it_Type.ContainsGenericParameters)
                    {
                        continue;
                    }

                    foreach (FieldInfo it_Field in it_Type.GetFields(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                    {
                        if (it_Field.IsLiteral || l_Findings.Count >= c_MaxFindings)
                        {
                            continue;
                        }

                        object? l_Value;
                        try
                        {
                            l_Value = it_Field.GetValue(null);
                        }
                        catch (Exception)
                        {
                            continue;
                        }

                        if (Find(l_Value, l_Context) is string l_Held)
                        {
                            l_Findings.Add($"{it_Type.FullName}.{it_Field.Name} holds {l_Held}");
                        }
                        else if (FindInFields(l_Value, l_Context) is (string l_Member, string l_Inner))
                        {
                            l_Findings.Add($"{it_Type.FullName}.{it_Field.Name}.{l_Member} holds {l_Inner}");
                        }
                    }
                }
            }

            return l_Findings.Count != 0 ? $"Still held by: {string.Join("; ", l_Findings)}. Let go of these as the script is destroyed, in OnDestroy" : "No static field outside it holds it, so a thread, a timer or a task of the game's may still run";
        }

        [UnconditionalSuppressMessage("Trimming", "IL2026", Justification = "Run in Forge only, after a reload whose context was not collected, over whatever metadata the loaded assemblies have")]
        private static Type?[] GetTypes(Assembly assembly)
        {
            try
            {
                return assembly.GetTypes();
            }
            catch (ReflectionTypeLoadException exception)
            {
                return exception.Types;
            }
            catch (Exception)
            {
                return Array.Empty<Type>();
            }
        }

        // What of the game's the value holds, or null when nothing
        private static string? Find(object? value, AssemblyLoadContext context)
        {
            switch (value)
            {
                case null:
                case string:
                    {
                        return null;
                    }
                case Delegate l_Delegate:
                    {
                        foreach (Delegate it_Call in l_Delegate.GetInvocationList())
                        {
                            Type? l_Declaring = it_Call.Method.DeclaringType;
                            if ((l_Declaring != null && IsGame(l_Declaring, context)) || (it_Call.Target != null && IsGame(it_Call.Target.GetType(), context)))
                            {
                                return $"{l_Declaring?.FullName ?? "?"}.{it_Call.Method.Name}";
                            }
                        }

                        return null;
                    }
            }

            if (IsGame(value.GetType(), context))
            {
                return $"an instance of {value.GetType().FullName}";
            }

            // A collection already filled is looked through, and nothing lazy is enumerated
            if (value is ICollection l_Collection)
            {
                int l_Seen = 0;
                foreach (object? it_Item in l_Collection)
                {
                    if (++l_Seen > c_MaxItems)
                    {
                        break;
                    }

                    if (it_Item is not ICollection && Find(it_Item, context) is string l_Held)
                    {
                        return $"{l_Held}, in a {value.GetType().Name}";
                    }
                }
            }

            return null;
        }

        // The first field of an object outside the game that holds something of the game's, with what it holds
        [UnconditionalSuppressMessage("Trimming", "IL2075", Justification = "Run in Forge only, after a reload whose context was not collected, over whatever metadata the loaded assemblies have")]
        private static (string Member, string Held)? FindInFields(object? value, AssemblyLoadContext context)
        {
            if (value == null || value is string || value is Delegate || value is ICollection || value is MemberInfo || value is Assembly || value.GetType().IsValueType)
            {
                return null;
            }

            foreach (FieldInfo it_Field in value.GetType().GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
            {
                object? l_Value;
                try
                {
                    l_Value = it_Field.GetValue(value);
                }
                catch (Exception)
                {
                    continue;
                }

                if (Find(l_Value, context) is string l_Held)
                {
                    return (it_Field.Name, l_Held);
                }
            }

            return null;
        }

        // A type of the game's, or one built from one, such as a List of them
        private static bool IsGame(Type type, AssemblyLoadContext context)
        {
            if (AssemblyLoadContext.GetLoadContext(type.Assembly) == context)
            {
                return true;
            }

            if (type.HasElementType && type.GetElementType() is Type l_Element && IsGame(l_Element, context))
            {
                return true;
            }

            if (type.IsGenericType)
            {
                foreach (Type it_Argument in type.GetGenericArguments())
                {
                    if (IsGame(it_Argument, context))
                    {
                        return true;
                    }
                }
            }

            return false;
        }
    }
}