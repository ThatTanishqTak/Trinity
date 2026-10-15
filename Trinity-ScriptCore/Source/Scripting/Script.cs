using System;
using System.Collections;
using System.Runtime.CompilerServices;
using Trinity.Interop;
using Trinity.Scripting;

namespace Trinity
{
    // The base of a C# script class, of which the engine makes one instance for each script on an entity once its scene runs. Its fields are its public fields and those marked [Field], which Properties shows and the scene saves, and their defaults are the values a new instance has. OnCreate runs before any other callback, after the physics bodies exist, and only once the script is enabled. OnFixedUpdate runs each fixed step ahead of physics, the physics callbacks after the step that raised them, OnUpdate once a frame after the steps, and OnLateUpdate after the transform pass. OnDestroy runs when the entity or the scene goes, for a script that had OnCreate. What it reaches beyond its fields goes through its Entity, its timers and coroutines, the static Time, Input, Physics and DebugDraw, and Log. An exception a callback throws is logged with its stack trace, and that script is called no more while play goes on. Main thread only
    public abstract class Script
    {
        // A field rather than a property, so a script sets what it reaches through it, as in Entity.Position = value, which C# refuses on a value a property returns
        public readonly Entity Entity;

        internal ScriptClass? Class { get; private set; }

        // The GC handle the engine knows it by, 0 until the engine has made it
        internal nint Handle { get; set; }

        public virtual void OnCreate() { }
        public virtual void OnFixedUpdate(float step) { }
        public virtual void OnUpdate(float timestep) { }
        public virtual void OnLateUpdate(float timestep) { }
        public virtual void OnDestroy() { }
        // In place of OnCreate, for an instance made again by a reload while the scene runs, to rebuild what it keeps outside its fields
        public virtual void OnReloaded() { }

        public virtual void OnContactBegin(Collision collision) { }
        public virtual void OnContactStay(Collision collision) { }
        public virtual void OnContactEnd(Collision collision) { }
        public virtual void OnTriggerEnter(Collision collision) { }
        public virtual void OnTriggerExit(Collision collision) { }
        public virtual void OnJointBreak(Collision collision) { }

        // Runs the coroutine at once until it first yields, then as each of its waits is met, as YieldInstruction says. An exception it throws stops this script, as one a callback throws does. Default for one that finished without waiting. Throws while no scene runs the script
        public Coroutine StartCoroutine(IEnumerator routine)
        {
            ArgumentNullException.ThrowIfNull(routine);
            ScriptTask l_Task = new(this, routine);
            TaskWait l_First = l_Task.Resume();

            return l_First.Type == WaitType.Done ? default : new Coroutine(ScriptHost.StartTask(l_Task, TaskKind.Coroutine, l_First, 0.0f));
        }

        // False when it had already finished or been stopped. One that stops itself goes once it next yields
        public bool StopCoroutine(Coroutine coroutine) => coroutine.ID != 0 && Native.Scripting_StopTask(coroutine.ID);

        public void StopAllCoroutines() => Native.Scripting_StopTasks(Handle, (int)TaskKind.Coroutine);

        public bool IsRunning(Coroutine coroutine) => coroutine.ID != 0 && Native.Scripting_IsTaskRunning(coroutine.ID);

        // The action after the delay in seconds of the scene's fixed steps, in the first step that reaches it, after the scripts' OnFixedUpdate and ahead of physics, so it holds while the scene is paused and moves on with Step. A repeating one runs again every interval after that, which must be more than 0. A script's timers and coroutines go with it. Throws while no scene runs the script
        public Timer Invoke(Action action, float delay)
        {
            ArgumentNullException.ThrowIfNull(action);

            return new Timer(ScriptHost.StartTask(new ScriptTask(this, action), TaskKind.Timer, TaskWait.ForSeconds(delay), 0.0f));
        }

        public Timer InvokeRepeating(Action action, float delay, float interval)
        {
            ArgumentNullException.ThrowIfNull(action);
            if (!(interval > 0.0f))
            {
                throw new ArgumentOutOfRangeException(nameof(interval), interval, "A repeating timer needs an interval of more than 0 seconds");
            }

            return new Timer(ScriptHost.StartTask(new ScriptTask(this, action), TaskKind.Timer, TaskWait.ForSeconds(delay), interval));
        }

        // False when it had already run for the last time or been cancelled
        public bool CancelInvoke(Timer timer) => timer.ID != 0 && Native.Scripting_StopTask(timer.ID);

        public void CancelInvokes() => Native.Scripting_StopTasks(Handle, (int)TaskKind.Timer);

        public bool IsPending(Timer timer) => timer.ID != 0 && Native.Scripting_IsTaskRunning(timer.ID);

        internal void Bind(Entity entity, ScriptClass scriptClass)
        {
            Unsafe.AsRef(in Entity) = entity;
            Class = scriptClass;
        }
    }
}