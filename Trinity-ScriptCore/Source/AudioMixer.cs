using Trinity.Interop;

namespace Trinity
{
    // The project's mixer, the only one its scenes play through, as Unity's AudioMixer: the values it exposes by name, which a script sets, holding through every snapshot transition until it clears them, and its snapshots, which it moves to over a time, each value along its curve. Each is false for a name the mixer lacks, and while no scene runs. A field of AssetRef<AudioMixerAsset> reaches it the same way, as Unity's mixer fields do, and does nothing when it names another mixer, with one warning, or none. Main thread only
    public static unsafe class AudioMixer
    {
        // The project's mixer asset, or None for a project with none, whose scenes play through Master alone
        public static AssetRef<AudioMixerAsset> Asset => new(Native.AudioMixer_GetAsset());

        // An exposed value, within its range
        public static bool SetFloat(string name, float value) => Set(0, name, value);

        // The value set, or else where the snapshots have it now. False, and 0, for a name the mixer does not expose
        public static bool GetFloat(string name, out float value) => Get(0, name, out value);

        // Back to where the snapshots take it
        public static bool ClearFloat(string name) => Clear(0, name);

        // Every value from where it is to the snapshot's over the time, each along the snapshot's curve for it, and at once for 0
        public static bool TransitionToSnapshot(string name, float seconds) => Transition(0, name, seconds);

        // The first group by that name, Master first, for an AudioSource's Output. None for a name the mixer lacks
        public static AudioMixerGroup FindGroup(string name) => Find(0, name);

        public static bool SetFloat(this AssetRef<AudioMixerAsset> mixer, string name, float value) => mixer.IsValid && Set(mixer.ID, name, value);

        public static bool GetFloat(this AssetRef<AudioMixerAsset> mixer, string name, out float value)
        {
            value = 0.0f;

            return mixer.IsValid && Get(mixer.ID, name, out value);
        }

        public static bool ClearFloat(this AssetRef<AudioMixerAsset> mixer, string name) => mixer.IsValid && Clear(mixer.ID, name);

        public static bool TransitionToSnapshot(this AssetRef<AudioMixerAsset> mixer, string name, float seconds) => mixer.IsValid && Transition(mixer.ID, name, seconds);

        public static AudioMixerGroup FindGroup(this AssetRef<AudioMixerAsset> mixer, string name) => mixer.IsValid ? Find(mixer.ID, name) : AudioMixerGroup.None;

        // The engine names the project's mixer by 0
        private static bool Set(ulong mixer, string name, float value)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                return Native.AudioMixer_SetFloat(mixer, new Utf8(l_Data, l_Name.Bytes.Length), value);
            }
        }

        private static bool Get(ulong mixer, string name, out float value)
        {
            float l_Value = 0.0f;
            bool l_Found;
            using (Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]))
            {
                fixed (byte* l_Data = l_Name.Bytes)
                {
                    l_Found = Native.AudioMixer_GetFloat(mixer, new Utf8(l_Data, l_Name.Bytes.Length), &l_Value);
                }
            }

            value = l_Value;

            return l_Found;
        }

        private static bool Clear(ulong mixer, string name)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                return Native.AudioMixer_ClearFloat(mixer, new Utf8(l_Data, l_Name.Bytes.Length));
            }
        }

        private static bool Transition(ulong mixer, string name, float seconds)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                return Native.AudioMixer_TransitionToSnapshot(mixer, new Utf8(l_Data, l_Name.Bytes.Length), seconds);
            }
        }

        private static AudioMixerGroup Find(ulong mixer, string name)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                return new AudioMixerGroup(Native.AudioMixer_FindGroup(mixer, new Utf8(l_Data, l_Name.Bytes.Length)));
            }
        }
    }
}