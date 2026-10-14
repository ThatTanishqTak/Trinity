using System;
using System.Diagnostics.CodeAnalysis;

namespace Trinity.Interop
{
    // How the generated components read and write each property, by its number in ScriptComponents.def, through a FieldValue on the stack. A failure throws, naming the entity and the property
    internal static unsafe partial class ComponentAccess
    {
        public static FieldValue Get(Entity entity, int property)
        {
            FieldValue l_Value;
            AccessResult l_Result = (AccessResult)Native.Component_Get(entity.ID, property, &l_Value);
            if (l_Result != AccessResult.Done)
            {
                Throw(entity, property, l_Result);
            }

            return l_Value;
        }

        public static void Set(Entity entity, int property, FieldValue value)
        {
            AccessResult l_Result = (AccessResult)Native.Component_Set(entity.ID, property, &value);
            if (l_Result != AccessResult.Done)
            {
                Throw(entity, property, l_Result);
            }
        }

        // For a function of the entity's own, what it lacks or what was wrong with what it was given
        public static void Check(Entity entity, AccessResult result, string missing, string? badValue = null)
        {
            switch (result)
            {
                case AccessResult.Done:
                    {
                        return;
                    }
                case AccessResult.NoEntity:
                    {
                        throw new InvalidOperationException($"The running scene has no entity {entity}");
                    }
                case AccessResult.NoComponent:
                    {
                        throw new InvalidOperationException($"Entity {entity} {missing}");
                    }
                default:
                    {
                        throw new InvalidOperationException($"Entity {entity} {badValue ?? missing}");
                    }
            }
        }

        [DoesNotReturn]
        private static void Throw(Entity entity, int property, AccessResult result)
        {
            string l_Property = (uint)property < (uint)s_PropertyNames.Length ? s_PropertyNames[property] : property.ToString();
            string l_Component = l_Property.Substring(0, Math.Max(l_Property.IndexOf('.'), 0));
            throw result switch
            {
                AccessResult.NoEntity => new InvalidOperationException($"The running scene has no entity {entity}"),
                AccessResult.NoComponent => new InvalidOperationException($"Entity {entity} has no {l_Component}"),
                _ => new ArgumentOutOfRangeException("value", $"{l_Property} of entity {entity} cannot take that value")
            };
        }
    }
}