using System;
using System.Collections;
using System.Runtime.CompilerServices;
using Trinity.Interop;
using Trinity.Scripting;

namespace Trinity
{
    // The base of a C# script class. A callback that throws is logged, and the script is called no more
    public abstract class Script
    {
        // A field, not a property, so Entity.Position = value compiles, which C# refuses on a returned one
        public readonly Entity Entity;

        internal ScriptClass? Class { get; private set; }

        // The GC handle the engine knows it by, 0 until the engine has made it
        internal nint Handle { get; set; }

        public virtual void OnCreate() { }
        public virtual void OnFixedUpdate(float step) { }
        public virtual void OnUpdate(float timestep) { }
        public virtual void OnLateUpdate(float timestep) { }
        public virtual void OnDestroy() { }
        // In place of OnCreate, for an instance made again by a reload, to rebuild what its fields lack
        public virtual void OnReloaded() { }

        public virtual void OnContactBegin(Collision collision) { }
        public virtual void OnContactStay(Collision collision) { }
        public virtual void OnContactEnd(Collision collision) { }
        public virtual void OnTriggerEnter(Collision collision) { }
        public virtual void OnTriggerExit(Collision collision) { }
        public virtual void OnJointBreak(Collision collision) { }
        // The entity's character began touching another body in the step. Once, until it moves away
        public virtual void OnCharacterHit(Collision hit) { }

        // Runs at once until it first yields. Default if it finished without waiting. Throws with no scene
        public Coroutine StartCoroutine(IEnumerator routine)
        {
            ArgumentNullException.ThrowIfNull(routine);
            ScriptTask l_Task = new(this, routine);
            TaskWait l_First = l_Task.Resume();

            return l_First.Type == WaitType.Done ? default : new Coroutine(ScriptHost.StartTask(l_Task, TaskKind.Coroutine, l_First, 0.0f));
        }

        // False when it had already finished or been stopped. One that stops itself goes at its next yield
        public bool StopCoroutine(Coroutine coroutine) => coroutine.ID != 0 && Native.Scripting_StopTask(coroutine.ID);

        public void StopAllCoroutines() => Native.Scripting_StopTasks(Handle, (int)TaskKind.Coroutine);

        public bool IsRunning(Coroutine coroutine) => coroutine.ID != 0 && Native.Scripting_IsTaskRunning(coroutine.ID);

        // In seconds of the scene's fixed steps. An interval must be above 0. Throws with no scene
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