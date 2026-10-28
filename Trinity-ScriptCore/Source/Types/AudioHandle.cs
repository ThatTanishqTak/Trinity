using System;
using Trinity.Interop;

namespace Trinity
{
    // A sound played on its own, by an AudioSource's PlayOneShot or by Audio's PlayAtPoint and PlayAttached, as the engine's AudioHandle is, which a script keeps to stop it, with a fade or without, set its sound event's parameters and move it. Each is new for the life of the process, so once its sound has ended, or the scene it played in has stopped, it reaches nothing: every call on it does nothing, and it reads as not playing. None for a sound that was not played. Main thread only
    public readonly struct AudioHandle : IEquatable<AudioHandle>
    {
        internal AudioHandle(ulong handle)
        {
            Handle = handle;
        }

        public static AudioHandle None => default;

        // The engine's handle, 0 for None
        internal ulong Handle { get; }

        // Until its sound ends, its fade out included. Not while a one-shot's source is paused
        public bool IsPlaying => Native.AudioHandle_IsPlaying(Handle);

        // Where it is in its clip, or how long its sound event has played, in seconds
        public double Time => Native.AudioHandle_GetTime(Handle);

        // At once with no fade, and otherwise fading out over that many seconds, heard meanwhile though no longer counted against its event's limit
        public void Stop(float fadeSeconds = 0.0f) => Native.AudioHandle_Stop(Handle, fadeSeconds);

        // A value of a parameter of its sound event, by its name, a one-shot's being its source's, as AudioSource.SetParameter has it. False once its sound has ended
        public unsafe bool SetParameter(string name, float value)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                return Native.AudioHandle_SetParameter(Handle, new Utf8(l_Data, l_Name.Bytes.Length), value);
            }
        }

        // By one of the parameter's labels, as its sound event has them. False too for a label the event lacks, or before the event has loaded
        public unsafe bool SetParameterLabel(string name, string label)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            using Utf8String l_Label = new(label, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_NameData = l_Name.Bytes)
            fixed (byte* l_LabelData = l_Label.Bytes)
            {
                return Native.AudioHandle_SetParameterLabel(Handle, new Utf8(l_NameData, l_Name.Bytes.Length), new Utf8(l_LabelData, l_Label.Bytes.Length));
            }
        }

        // The value it was given, or else its sound event's default. False, and 0, for a parameter it has neither of
        public unsafe bool GetParameter(string name, out float value)
        {
            float l_Value = 0.0f;
            bool l_Found;
            using (Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]))
            {
                fixed (byte* l_Data = l_Name.Bytes)
                {
                    l_Found = Native.AudioHandle_GetParameter(Handle, new Utf8(l_Data, l_Name.Bytes.Length), &l_Value);
                }
            }

            value = l_Value;

            return l_Found;
        }

        // Where a sound played at a point, or attached, plays from now on, following no entity. Nothing for a one-shot, which follows its source
        public unsafe void SetPosition(Vector3 position) => Native.AudioHandle_SetPosition(Handle, &position);

        // From now on following the entity, as PlayAttached does. Nothing for a one-shot, or for an entity the running scene does not have
        public void Follow(Entity entity) => Native.AudioHandle_Follow(Handle, entity.ID);

        public bool Equals(AudioHandle other) => Handle == other.Handle;
        public override bool Equals(object? obj) => obj is AudioHandle other && Equals(other);
        public override int GetHashCode() => Handle.GetHashCode();
        public override string ToString() => Handle.ToString();

        public static bool operator ==(AudioHandle left, AudioHandle right) => left.Equals(right);
        public static bool operator !=(AudioHandle left, AudioHandle right) => !left.Equals(right);
    }
}