using System.Runtime.CompilerServices;
using Trinity.Scripting;

namespace Trinity
{
    // The base of a C# script class, of which the engine makes one instance for each script on an entity once its scene runs. Its fields are its public fields and those marked [Field], which Properties shows and the scene saves, and their defaults are the values a new instance has. OnCreate runs before any other callback, after the physics bodies exist, and only once the script is enabled. OnFixedUpdate runs each fixed step ahead of physics, the physics callbacks after the step that raised them, OnUpdate once a frame after the steps, and OnLateUpdate after the transform pass. OnDestroy runs when the entity or the scene goes, for a script that had OnCreate. What it reaches beyond its fields goes through its Entity, the static Time, Input and DebugDraw, and Log. An exception a callback throws is logged with its stack trace, and that script is called no more while play goes on. Main thread only
    public abstract class Script
    {
        // A field rather than a property, so a script sets what it reaches through it, as in Entity.Position = value, which C# refuses on a value a property returns
        public readonly Entity Entity;

        internal ScriptClass? Class { get; private set; }

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

        internal void Bind(Entity entity, ScriptClass scriptClass)
        {
            Unsafe.AsRef(in Entity) = entity;
            Class = scriptClass;
        }
    }
}