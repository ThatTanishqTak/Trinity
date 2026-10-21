using System;
using System.ComponentModel;

namespace Trinity.Scripting
{
    // The callbacks a script class overrides, so the engine calls only those, valued as the engine's ScriptCallbacks are
    [EditorBrowsable(EditorBrowsableState.Never)]
    [Flags]
    public enum ScriptCallbacks : uint
    {
        None = 0,
        Create = 1u << 0,
        FixedUpdate = 1u << 1,
        Update = 1u << 2,
        LateUpdate = 1u << 3,
        Destroy = 1u << 4,
        // Any of the contact, trigger, joint and character hit callbacks
        Physics = 1u << 5,
        Reloaded = 1u << 6
    }
}