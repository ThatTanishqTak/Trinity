using Trinity.Interop;

namespace Trinity
{
    // The running scene's sounds played on their own, as Unity's AudioSource.PlayClipAtPoint plays a clip, each from a memory of its own, and the global parameters every sound event hears, as FMOD's. A clip plays spatialized as a source does by default, and a sound event with its own 3D settings, priority and Output. The handle each gives reaches its sound until it ends. Nothing plays while no scene runs. Main thread only
    public static unsafe class Audio
    {
        // At a place of its own. None for a resource that is None, or while no scene runs
        public static AudioHandle PlayAtPoint<T>(AssetRef<T> resource, Vector3 position, float volume = 1.0f) where T : IAudioResource
        {
            return new AudioHandle(Native.Audio_PlayAtPoint(resource.ID, &position, volume));
        }

        // Following the entity as a source would, until it is gone, then staying where it was. None for an entity the running scene does not have
        public static AudioHandle PlayAttached<T>(AssetRef<T> resource, Entity entity, float volume = 1.0f) where T : IAudioResource
        {
            return new AudioHandle(Native.Audio_PlayAttached(resource.ID, entity.ID, volume));
        }

        // A value every sound event playing with a global parameter by that name hears, from the same period, each seeking towards it at its own speed, as do those played after it. False while no scene runs
        public static bool SetGlobalParameter(string name, float value)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                return Native.Audio_SetGlobalParameter(new Utf8(l_Data, l_Name.Bytes.Length), value);
            }
        }

        // The value given. False, and 0, before one is
        public static bool GetGlobalParameter(string name, out float value)
        {
            float l_Value = 0.0f;
            bool l_Found;
            using (Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]))
            {
                fixed (byte* l_Data = l_Name.Bytes)
                {
                    l_Found = Native.Audio_GetGlobalParameter(new Utf8(l_Data, l_Name.Bytes.Length), &l_Value);
                }
            }

            value = l_Value;

            return l_Found;
        }
    }
}