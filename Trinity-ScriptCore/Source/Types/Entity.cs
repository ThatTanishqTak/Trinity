using System;
using System.Runtime.InteropServices;
using System.Text;
using Trinity.Interop;

namespace Trinity
{
    // An entity, named by its UUID, so it stays the same entity through a reload and in a scene that was saved and loaded again. None has the UUID 0. What it reaches is the running scene's: its tag, its place in the hierarchy and in the world, its components through Get, and its scripts. Each reads and writes the engine's own at once, and one that finds no such entity, or no such component, throws InvalidOperationException, which stops the script that called it. Main thread only
    public readonly struct Entity : IEquatable<Entity>
    {
        public static Entity None => default;

        public ulong ID { get; }

        public bool IsValid => ID != 0;

        public Entity(ulong id)
        {
            ID = id;
        }

        // Whether the running scene has it, which a destroyed one stops being at the end of the frame
        public bool Exists => Native.Entity_Exists(ID);

        // None when the running scene has no such entity
        public static Entity Find(ulong id) => Native.Entity_Exists(id) ? new Entity(id) : None;

        // The first in hierarchy order with that tag, or None
        public static unsafe Entity FindByName(string name)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                return new Entity(Native.Entity_FindByName(new Utf8(l_Data, l_Name.Bytes.Length)));
            }
        }

        // At once, last under the parent, or last of the roots for None
        public static unsafe Entity Create(string name = "Entity", Entity parent = default)
        {
            ulong l_Created;
            using (Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]))
            {
                fixed (byte* l_Data = l_Name.Bytes)
                {
                    l_Created = Native.Entity_Create(new Utf8(l_Data, l_Name.Bytes.Length), parent.ID);
                }
            }

            return l_Created != 0 ? new Entity(l_Created) : throw new InvalidOperationException($"No entity was made, since the running scene has no parent {parent}");
        }

        // An instance of the prefab, made at once from its template in memory, as Unity's Instantiate makes one: last of the roots, or under the parent with the prefab's transform, or with its root at the position and rotation in the world, under the parent too. Its scripts are made at once with their fields, and have OnCreate before their first call, by the end of the frame at the latest, and its bodies and characters join physics at the next step. None, logged once a prefab, for a prefab the running scene does not hold, as it holds every prefab its fields name, or that cannot be read. Throws for a parent the running scene does not have
        public static unsafe Entity Instantiate(AssetRef<PrefabAsset> prefab, Entity parent = default)
        {
            CheckParent(prefab, parent);

            return new Entity(Native.Entity_Instantiate(prefab.ID, parent.ID, null, null));
        }

        public static unsafe Entity Instantiate(AssetRef<PrefabAsset> prefab, Vector3 position, Quaternion rotation, Entity parent = default)
        {
            CheckParent(prefab, parent);

            return new Entity(Native.Entity_Instantiate(prefab.ID, parent.ID, &position, &rotation));
        }

        // It and its subtree go at the end of the frame, after every script has run, so a script may destroy its own entity and finish the call it is in
        public void Destroy()
        {
            if (!Native.Entity_Destroy(ID))
            {
                ThrowMissing();
            }
        }

        // It and its subtree copied at once, placed after it, with entity fields inside the copy pointing at the copy's own entities. The copy's scripts are made at once and have OnCreate before their first call, by the end of the frame at the latest
        public Entity Duplicate()
        {
            ulong l_Copy = Native.Entity_Duplicate(ID);

            return l_Copy != 0 ? new Entity(l_Copy) : ThrowMissing<Entity>();
        }

        // Its tag, which names it in the Scene panel
        public unsafe string Name
        {
            get
            {
                if (!Exists)
                {
                    ThrowMissing();
                }

                Utf8 l_Name = Native.Entity_GetName(ID);

                return l_Name.Length > 0 ? Encoding.UTF8.GetString(l_Name.Data, l_Name.Length) : string.Empty;
            }
            set
            {
                bool l_Set;
                using (Utf8String l_Name = new(value, stackalloc byte[Utf8String.c_StackBytes]))
                {
                    fixed (byte* l_Data = l_Name.Bytes)
                    {
                        l_Set = Native.Entity_SetName(ID, new Utf8(l_Data, l_Name.Bytes.Length));
                    }
                }

                if (!l_Set)
                {
                    ThrowMissing();
                }
            }
        }

        // None for a root. Setting it keeps the entity where it is in the world, and SetParent can keep its transform instead
        public Entity Parent
        {
            get => Exists ? new Entity(Native.Entity_GetParent(ID)) : ThrowMissing<Entity>();
            set => SetParent(value);
        }

        // Last under the parent, or last of the roots for None. Refused, with an exception, for a parent the running scene does not have or one inside this entity's own subtree
        public void SetParent(Entity parent, bool keepWorldTransform = true)
        {
            if (!Native.Entity_SetParent(ID, parent.ID, keepWorldTransform))
            {
                throw new InvalidOperationException(Exists ? $"Entity {this} cannot go under {parent}, which the running scene does not have, or which is inside it" : $"The running scene has no entity {this}");
            }
        }

        public int ChildCount => Exists ? (int)Native.Entity_GetChildCount(ID) : ThrowMissing<int>();

        // The children in order, read as the walk goes, which allocates nothing
        public ChildEnumerable Children => Exists ? new ChildEnumerable(this) : ThrowMissing<ChildEnumerable>();

        // Relative to the parent, as its Transform has them
        public Vector3 Position
        {
            get => new Transform(this).Position;
            set => new Transform(this).Position = value;
        }

        public Quaternion Rotation
        {
            get => new Transform(this).Rotation;
            set => new Transform(this).Rotation = value;
        }

        public Vector3 Scale
        {
            get => new Transform(this).Scale;
            set => new Transform(this).Scale = value;
        }

        // In the world, from its transform and its parents' as they are now, with any shear dropped. Setting one sets its transform relative to its parent, and a running body is teleported there
        public unsafe Vector3 WorldPosition
        {
            get
            {
                GetWorld(out Vector3 l_Position, out _, out _);

                return l_Position;
            }
            set
            {
                if (!Native.Transform_SetWorld(ID, &value, null, null))
                {
                    ThrowMissing();
                }
            }
        }

        public unsafe Quaternion WorldRotation
        {
            get
            {
                GetWorld(out _, out Quaternion l_Rotation, out _);

                return l_Rotation;
            }
            set
            {
                if (!Native.Transform_SetWorld(ID, null, &value, null))
                {
                    ThrowMissing();
                }
            }
        }

        public unsafe Vector3 WorldScale
        {
            get
            {
                GetWorld(out _, out _, out Vector3 l_Scale);

                return l_Scale;
            }
            set
            {
                if (!Native.Transform_SetWorld(ID, null, null, &value))
                {
                    ThrowMissing();
                }
            }
        }

        public bool Has<T>() where T : struct, IComponent<T> => Native.Component_Has(ID, T.TypeId);

        // Its component, which reads and writes the engine's own. Throws when it has none
        public T Get<T>() where T : struct, IComponent<T>
        {
            if (!Native.Component_Has(ID, T.TypeId))
            {
                throw new InvalidOperationException(Exists ? $"Entity {this} has no {T.TypeName}" : $"The running scene has no entity {this}");
            }

            return T.Bind(this);
        }

        public bool TryGet<T>(out T component) where T : struct, IComponent<T>
        {
            bool l_Has = Native.Component_Has(ID, T.TypeId);
            component = l_Has ? T.Bind(this) : default;

            return l_Has;
        }

        // A new one with the engine's defaults, which a running scene's physics picks up at the next step. Throws when it has one already, and for Transform, which every entity has
        public T Add<T>() where T : struct, IComponent<T>
        {
            ComponentAccess.Check(this, (AccessResult)Native.Component_Add(ID, T.TypeId), $"already has a {T.TypeName}");

            return T.Bind(this);
        }

        // Throws when it has none, and for Transform, which an entity keeps until it is destroyed
        public void Remove<T>() where T : struct, IComponent<T>
        {
            ComponentAccess.Check(this, (AccessResult)Native.Component_Remove(ID, T.TypeId), typeof(T) == typeof(Transform) ? "keeps its Transform until it is destroyed" : $"has no {T.TypeName}");
        }

        public int ScriptCount => Native.Scripting_GetScriptCount(ID);

        // The first C# script of the class on the entity, or null
        public T? GetScript<T>() where T : Script
        {
            int l_Count = Native.Scripting_GetScriptCount(ID);
            for (int it_Index = 0; it_Index < l_Count; ++it_Index)
            {
                if (GetInstance(it_Index) is T l_Script)
                {
                    return l_Script;
                }
            }

            return null;
        }

        public bool HasScript<T>() where T : Script => GetScript<T>() != null;

        // Whether it has a script of the class, in either language, whether or not its code is loaded
        public bool HasScript(string className, ScriptLanguage language = ScriptLanguage.CSharp) => FindScript(className, language) >= 0;

        // A script of the class last on the entity, with its fields at their defaults, made at once and given OnCreate before its first call, by the end of the frame at the latest. Throws when the class is not loaded
        public T AddScript<T>() where T : Script
        {
            int l_Index = AddScript(ScriptName<T>.Value, ScriptLanguage.CSharp);

            return GetInstance(l_Index) as T ?? throw new InvalidOperationException($"{ScriptName<T>.Value} was added to entity {this}, but its constructor threw");
        }

        // Its index on the entity. Throws when no loaded code has the class
        public unsafe int AddScript(string className, ScriptLanguage language)
        {
            int l_Index;
            using (Utf8String l_Name = new(className, stackalloc byte[Utf8String.c_StackBytes]))
            {
                fixed (byte* l_Data = l_Name.Bytes)
                {
                    l_Index = Native.Scripting_AddScript(ID, (int)language, new Utf8(l_Data, l_Name.Bytes.Length));
                }
            }

            return l_Index >= 0 ? l_Index : throw new InvalidOperationException(Exists ? $"No loaded code has {language} class {className}, so it was not added to entity {this}" : $"The running scene has no entity {this}");
        }

        // The first script of the class goes at the end of the frame, after its OnDestroy, and stays until then. False when it has none
        public bool RemoveScript<T>() where T : Script
        {
            int l_Count = Native.Scripting_GetScriptCount(ID);
            for (int it_Index = 0; it_Index < l_Count; ++it_Index)
            {
                if (GetInstance(it_Index) is T)
                {
                    return Native.Scripting_RemoveScript(ID, it_Index);
                }
            }

            return false;
        }

        public bool RemoveScript(string className, ScriptLanguage language = ScriptLanguage.CSharp)
        {
            int l_Index = FindScript(className, language);

            return l_Index >= 0 && Native.Scripting_RemoveScript(ID, l_Index);
        }

        public bool Equals(Entity other) => ID == other.ID;
        public override bool Equals(object? obj) => obj is Entity other && Equals(other);
        public override int GetHashCode() => ID.GetHashCode();
        public override string ToString() => ID.ToString("x16");

        public static bool operator ==(Entity left, Entity right) => left.Equals(right);
        public static bool operator !=(Entity left, Entity right) => !left.Equals(right);

        private unsafe void GetWorld(out Vector3 position, out Quaternion rotation, out Vector3 scale)
        {
            Vector3 l_Position;
            Quaternion l_Rotation;
            Vector3 l_Scale;
            if (!Native.Transform_GetWorld(ID, &l_Position, &l_Rotation, &l_Scale))
            {
                ThrowMissing();
            }

            position = l_Position;
            rotation = l_Rotation;
            scale = l_Scale;
        }

        private object? GetInstance(int index)
        {
            long l_Handle = Native.Scripting_GetInstance(ID, index, (int)ScriptLanguage.CSharp);

            return l_Handle != 0 ? GCHandle.FromIntPtr((nint)l_Handle).Target : null;
        }

        private unsafe int FindScript(string className, ScriptLanguage language)
        {
            using Utf8String l_Name = new(className, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                return Native.Scripting_FindScript(ID, (int)language, new Utf8(l_Data, l_Name.Bytes.Length));
            }
        }

        private void ThrowMissing() => throw new InvalidOperationException($"The running scene has no entity {this}");

        private T ThrowMissing<T>() => throw new InvalidOperationException($"The running scene has no entity {this}");

        private static void CheckParent(AssetRef<PrefabAsset> prefab, Entity parent)
        {
            if (parent.IsValid && !parent.Exists)
            {
                throw new InvalidOperationException($"No instance of prefab {prefab} was made, since the running scene has no parent {parent}");
            }
        }

        // The name the engine knows a C# script class by, as its source generator writes it
        private static class ScriptName<T> where T : Script
        {
            public static readonly string Value = typeof(T).FullName!.Replace('+', '.');
        }

        // An entity's children, walked as Entity's own functions walk them
        public readonly struct ChildEnumerable
        {
            private readonly Entity m_Parent;

            internal ChildEnumerable(Entity parent)
            {
                m_Parent = parent;
            }

            public ChildEnumerator GetEnumerator() => new(m_Parent);
        }

        public struct ChildEnumerator
        {
            private readonly Entity m_Parent;
            private bool m_Started;

            internal ChildEnumerator(Entity parent)
            {
                m_Parent = parent;
                m_Started = false;
                Current = None;
            }

            public Entity Current { get; private set; }

            public bool MoveNext()
            {
                Current = new Entity(m_Started ? Native.Entity_GetNextSibling(Current.ID) : Native.Entity_GetFirstChild(m_Parent.ID));
                m_Started = true;

                return Current.IsValid;
            }
        }
    }
}