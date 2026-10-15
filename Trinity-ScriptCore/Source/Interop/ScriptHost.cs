using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Trinity.Scripting;

namespace Trinity.Interop
{
    // A script callback, numbered as the engine's DotNetHost::ManagedCallback numbers it
    internal enum ScriptCallback
    {
        Create,
        FixedUpdate,
        Update,
        LateUpdate,
        Destroy,
        Reloaded
    }

    // The entry points the engine runs C# scripts through: a game assembly loads, its classes are described to the engine, and their instances are made, given their fields and called. An instance is a GC handle to it. Every exception is caught here, since none may cross into native code, and logged with its stack trace
    [EditorBrowsable(EditorBrowsableState.Never)]
    public static unsafe class ScriptHost
    {
        // How many times a full collection is asked for before an unloaded context counts as kept alive
        private const int c_CollectAttempts = 10;

        private static GameLoadContext? s_Context;
        private static string s_AssemblyName = string.Empty;
        private static readonly List<ScriptClass> s_Classes = new();
        private static readonly HashSet<nint> s_Instances = new();
        private static readonly HashSet<nint> s_Tasks = new();

        // The copy to load, and the build it was copied from, beside which what it depends on is found. The number of classes described, numbered from 0 in the order the engine was told of them, or -1 when the assembly could not be loaded
        [UnmanagedCallersOnly]
        public static int LoadAssembly(byte* path, int length, byte* buildPath, int buildLength)
        {
            try
            {
                return Load(Encoding.UTF8.GetString(path, length), Encoding.UTF8.GetString(buildPath, buildLength));
            }
            catch (Exception exception)
            {
                NativeLog.Write(LogLevel.Error, $"Scripting: the C# assembly could not be loaded: {exception}");

                return -1;
            }
        }

        // 1 once the context was collected, and 0 when something still holds it, which is logged by name
        [UnmanagedCallersOnly]
        public static int UnloadAssembly()
        {
            try
            {
                return Unload() ? 1 : 0;
            }
            catch (Exception exception)
            {
                NativeLog.Write(LogLevel.Error, $"Scripting: {s_AssemblyName} could not be unloaded: {exception}");

                return 0;
            }
        }

        // The managed heap's size as of the last collection, and how many collections there have been
        [UnmanagedCallersOnly]
        public static void GetHeapStatistics(long* bytes, int* collections)
        {
            *bytes = GC.GetTotalMemory(false);
            *collections = GC.CollectionCount(0);
        }

        // 0 when the class has no instance to give, logged when its constructor threw
        [UnmanagedCallersOnly]
        public static nint CreateInstance(int classIndex, ulong entity)
        {
            if ((uint)classIndex >= (uint)s_Classes.Count)
            {
                return 0;
            }

            ScriptClass l_Class = s_Classes[classIndex];
            try
            {
                Script l_Script = l_Class.Create();
                l_Script.Bind(new Entity(entity), l_Class);
                nint l_Handle = GCHandle.ToIntPtr(GCHandle.Alloc(l_Script));
                l_Script.Handle = l_Handle;
                s_Instances.Add(l_Handle);

                return l_Handle;
            }
            catch (Exception exception)
            {
                NativeLog.Write(LogLevel.Error, $"Scripting: {l_Class.Name} on entity {new Entity(entity)} could not be made, since its constructor threw: {exception}");

                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static void DestroyInstance(nint instance)
        {
            if (s_Instances.Remove(instance))
            {
                GCHandle.FromIntPtr(instance).Free();
            }
        }

        [UnmanagedCallersOnly]
        public static int SetField(nint instance, int field, FieldValue* value)
        {
            Script l_Script = GetScript(instance);
            try
            {
                l_Script.Class!.SetField(l_Script, field, *value);

                return 0;
            }
            catch (Exception exception)
            {
                Report(l_Script, $"took the value of its field {GetFieldName(l_Script, field)}", exception);

                return 1;
            }
        }

        [UnmanagedCallersOnly]
        public static int GetField(nint instance, int field, FieldValue* value)
        {
            Script l_Script = GetScript(instance);
            try
            {
                *value = l_Script.Class!.GetField(l_Script, field);

                return 0;
            }
            catch (Exception exception)
            {
                Report(l_Script, $"gave the value of its field {GetFieldName(l_Script, field)}", exception);

                return 1;
            }
        }

        // 0 once the callback has run, and 1 when it threw
        [UnmanagedCallersOnly]
        public static int Invoke(nint instance, int callback, float timestep)
        {
            Script l_Script = GetScript(instance);
            try
            {
                switch ((ScriptCallback)callback)
                {
                    case ScriptCallback.Create: l_Script.OnCreate(); break;
                    case ScriptCallback.FixedUpdate: l_Script.OnFixedUpdate(timestep); break;
                    case ScriptCallback.Update: l_Script.OnUpdate(timestep); break;
                    case ScriptCallback.LateUpdate: l_Script.OnLateUpdate(timestep); break;
                    case ScriptCallback.Destroy: l_Script.OnDestroy(); break;
                    case ScriptCallback.Reloaded: l_Script.OnReloaded(); break;
                }

                return 0;
            }
            catch (Exception exception)
            {
                Report(l_Script, $"threw in On{(ScriptCallback)callback}", exception);

                return 1;
            }
        }

        // The type is numbered as the engine's PhysicsEventType numbers it
        [UnmanagedCallersOnly]
        public static int InvokePhysics(nint instance, int type, Collision* collision)
        {
            Script l_Script = GetScript(instance);
            Trinity.Collision l_Collision = new(new Entity(collision->Other), collision->Point, collision->Normal, collision->Is2D);
            try
            {
                switch (type)
                {
                    case 0: l_Script.OnContactBegin(l_Collision); break;
                    case 1: l_Script.OnContactStay(l_Collision); break;
                    case 2: l_Script.OnContactEnd(l_Collision); break;
                    case 3: l_Script.OnTriggerEnter(l_Collision); break;
                    case 4: l_Script.OnTriggerExit(l_Collision); break;
                    case 5: l_Script.OnJointBreak(l_Collision); break;
                }

                return 0;
            }
            catch (Exception exception)
            {
                Report(l_Script, "threw in a physics callback", exception);

                return 1;
            }
        }

        // The wait it gives next is numbered as the engine's ScriptWaitType. 0 once it has run, and 1 when it threw, which stops its script
        [UnmanagedCallersOnly]
        public static int ResumeTask(nint task, int* waitType, float* seconds, int* frames)
        {
            ScriptTask l_Task = (ScriptTask)GCHandle.FromIntPtr(task).Target!;
            try
            {
                TaskWait l_Wait = l_Task.Resume();
                *waitType = (int)l_Wait.Type;
                *seconds = l_Wait.Seconds;
                *frames = l_Wait.Frames;

                return 0;
            }
            catch (Exception exception)
            {
                Report(l_Task.Owner, l_Task.IsTimer ? "threw in a timer" : "threw in a coroutine", exception);

                return 1;
            }
        }

        [UnmanagedCallersOnly]
        public static int CheckTask(nint task, int* met)
        {
            ScriptTask l_Task = (ScriptTask)GCHandle.FromIntPtr(task).Target!;
            try
            {
                *met = l_Task.IsMet() ? 1 : 0;

                return 0;
            }
            catch (Exception exception)
            {
                Report(l_Task.Owner, "threw in a coroutine's condition", exception);

                return 1;
            }
        }

        [UnmanagedCallersOnly]
        public static void FreeTask(nint task)
        {
            Free(task);
        }

        // Held by a GC handle the engine keeps until it frees it, which it does at once when no scene runs the script
        internal static ulong StartTask(ScriptTask task, TaskKind kind, TaskWait wait, float interval)
        {
            if (task.Owner.Handle == 0)
            {
                throw new InvalidOperationException($"A {(kind == TaskKind.Timer ? "timer" : "coroutine")} was not started, since the engine has not made the script that started it");
            }

            nint l_Handle = GCHandle.ToIntPtr(GCHandle.Alloc(task));
            s_Tasks.Add(l_Handle);
            ulong l_ID = Native.Scripting_StartTask(task.Owner.Handle, l_Handle, (int)kind, (int)wait.Type, wait.Seconds, wait.Frames, interval);

            return l_ID != 0 ? l_ID : throw new InvalidOperationException($"{task.Owner.Class!.Name} on entity {task.Owner.Entity} started a {(kind == TaskKind.Timer ? "timer" : "coroutine")} while no scene runs it");
        }

        private static void Free(nint task)
        {
            if (s_Tasks.Remove(task))
            {
                GCHandle.FromIntPtr(task).Free();
            }
        }

        private static Script GetScript(nint instance) => (Script)GCHandle.FromIntPtr(instance).Target!;

        private static string GetFieldName(Script script, int field) => (uint)field < (uint)script.Class!.Fields.Count ? script.Class.Fields[field].Name : field.ToString();

        // Once for the script, since the engine calls it no more
        private static void Report(Script script, string what, Exception exception)
        {
            NativeLog.Write(LogLevel.Error, $"Scripting: {script.Class!.Name} on entity {script.Entity} {what}, so it stops while play goes on: {exception}");
        }

        // The assembly's module initializer, which its source generator wrote, adds its classes as it runs
        private static int Load(string path, string buildPath)
        {
            string l_Name = Path.GetFileName(path);
            if (s_Context != null)
            {
                NativeLog.Write(LogLevel.Error, $"Scripting: {l_Name} is not loaded, since {s_AssemblyName} is");

                return -1;
            }

            GameLoadContext l_Context = new(buildPath.Length != 0 ? buildPath : path);
            List<ScriptClass> l_Found;
            try
            {
                Assembly l_Assembly = l_Context.LoadMain(path);
                ScriptRegistry.Take();
                RuntimeHelpers.RunModuleConstructor(l_Assembly.ManifestModule.ModuleHandle);
                l_Found = ScriptRegistry.Take();
            }
            catch (Exception exception)
            {
                NativeLog.Write(LogLevel.Error, $"Scripting: {l_Name} could not be loaded: {exception}");
                l_Context.Unload();

                return -1;
            }

            s_Context = l_Context;
            s_AssemblyName = l_Name;
            foreach (ScriptClass it_Class in l_Found)
            {
                Describe(it_Class);
            }

            return s_Classes.Count;
        }

        // Each field's default is the value a new instance has. A class whose constructor throws is left out, and is not numbered
        private static void Describe(ScriptClass scriptClass)
        {
            Script l_Defaults;
            try
            {
                l_Defaults = scriptClass.Create();
            }
            catch (Exception exception)
            {
                NativeLog.Write(LogLevel.Error, $"Scripting: {scriptClass.Name} is left out, since its constructor threw: {exception}");

                return;
            }

            s_Classes.Add(scriptClass);
            byte[] l_ClassName = Encoding.UTF8.GetBytes(scriptClass.Name);
            fixed (byte* l_ClassData = l_ClassName)
            {
                Native.Scripting_BeginClass(new Utf8(l_ClassData, l_ClassName.Length), (uint)scriptClass.Callbacks);
            }

            for (int it_Field = 0; it_Field < scriptClass.Fields.Count; ++it_Field)
            {
                ScriptField l_Field = scriptClass.Fields[it_Field];
                FieldValue l_Default;
                try
                {
                    l_Default = scriptClass.GetField(l_Defaults, it_Field);
                }
                catch (Exception exception)
                {
                    NativeLog.Write(LogLevel.Warn, $"Scripting: field {l_Field.Name} of {scriptClass.Name} could not be read, so it starts empty: {exception}");
                    l_Default = l_Field.Type == FieldType.String ? FieldValue.String(null) : new FieldValue { Type = l_Field.Type };
                }

                byte[] l_FieldName = Encoding.UTF8.GetBytes(l_Field.Name);
                byte[] l_AssetType = Encoding.UTF8.GetBytes(l_Field.AssetType);
                fixed (byte* l_NameData = l_FieldName)
                fixed (byte* l_AssetData = l_AssetType)
                {
                    Native.Scripting_AddField(new Utf8(l_NameData, l_FieldName.Length), &l_Default, new Utf8(l_AssetData, l_AssetType.Length));
                }

                foreach (ScriptEnumValue it_Value in l_Field.EnumValues)
                {
                    byte[] l_ValueName = Encoding.UTF8.GetBytes(it_Value.Name);
                    fixed (byte* l_ValueData = l_ValueName)
                    {
                        Native.Scripting_AddEnumValue(new Utf8(l_ValueData, l_ValueName.Length), it_Value.Value);
                    }
                }
            }

            if (!Native.Scripting_EndClass())
            {
                NativeLog.Write(LogLevel.Error, $"Scripting: {scriptClass.Name} was not registered, so its scripts do not run");
            }
        }

        // Every instance still held is freed and reported, since the context cannot be collected while one lives. Then collections are asked for until the context goes, and one that stays is reported with what still holds it
        private static bool Unload()
        {
            if (s_Context == null)
            {
                return true;
            }

            string l_Name = s_AssemblyName;
            if (s_Instances.Count != 0)
            {
                NativeLog.Write(LogLevel.Warn, $"Scripting: {s_Instances.Count} C# script instance(s) were still held as {l_Name} unloaded, and were freed");
                foreach (nint it_Instance in s_Instances)
                {
                    GCHandle.FromIntPtr(it_Instance).Free();
                }

                s_Instances.Clear();
            }

            if (s_Tasks.Count != 0)
            {
                NativeLog.Write(LogLevel.Warn, $"Scripting: {s_Tasks.Count} C# timer(s) and coroutine(s) were still held as {l_Name} unloaded, and were freed");
                foreach (nint it_Task in s_Tasks)
                {
                    GCHandle.FromIntPtr(it_Task).Free();
                }

                s_Tasks.Clear();
            }

            s_Classes.Clear();
            ScriptRegistry.Take();
            s_AssemblyName = string.Empty;
            WeakReference l_Context = Release();
            for (int it_Attempt = 0; it_Attempt < c_CollectAttempts && l_Context.IsAlive; ++it_Attempt)
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
            }

            if (!l_Context.IsAlive)
            {
                return true;
            }

            NativeLog.Write(LogLevel.Warn, $"Scripting: {l_Name} was not collected after it unloaded, so its memory stays until Forge closes. {LeakFinder.Describe(l_Context)}");

            return false;
        }

        // In a method of its own, so no local of the caller's still holds the context once it is let go
        [MethodImpl(MethodImplOptions.NoInlining)]
        private static WeakReference Release()
        {
            WeakReference l_Context = new(s_Context);
            s_Context!.Unload();
            s_Context = null;

            return l_Context;
        }
    }
}