using Trinity.Interop;

namespace Trinity
{
    // What a script does with the source as the scene runs, as Unity's AudioSource: Play from the start of its Resource, in place of what it plays, once what it names has loaded, Stop, Pause and UnPause, which take its one-shots with them, and PlayOneShot beside what it plays. The parameters of the sound events it plays are its own, kept for its later plays too. A value written to it is heard as the scene's sounds next update. An entity with no AudioSource throws
    public readonly unsafe partial struct AudioSource
    {
        private const string c_NoSource = "has no AudioSource";

        public void Play() => Check(Native.AudioSource_Play(m_Entity.ID));

        public void Stop() => Check(Native.AudioSource_Stop(m_Entity.ID));

        public void Pause() => Check(Native.AudioSource_Pause(m_Entity.ID));

        public void UnPause() => Check(Native.AudioSource_UnPause(m_Entity.ID));

        // From Play until what it plays ends or is stopped, while it waits for its clip too. Not while it is paused
        public bool IsPlaying
        {
            get
            {
                bool l_Playing;
                Check(Native.AudioSource_IsPlaying(m_Entity.ID, &l_Playing));

                return l_Playing;
            }
        }

        // Where it is in its clip, or how long its sound event has played, in seconds, 0 while it plays nothing
        public double Time
        {
            get
            {
                double l_Time;
                Check(Native.AudioSource_GetTime(m_Entity.ID, &l_Time));

                return l_Time;
            }
        }

        // A clip or a sound event as its Resource, which takes either
        public void SetResource<T>(AssetRef<T> resource) where T : IAudioResource => Resource = new AssetRef<AudioResourceAsset>(resource.ID);

        // Its own Resource beside what it plays rather than in its place, with its values and its volume scaled, following it, and stopped, paused and kept across scenes with it. It shares the source's parameters, and Avoid Repeat spans both. None for a source with no Resource
        public AudioHandle PlayOneShot(float volumeScale = 1.0f) => PlayOneShot(0, volumeScale);

        // A clip or a sound event in the same way. None for a resource that is None
        public AudioHandle PlayOneShot<T>(AssetRef<T> resource, float volumeScale = 1.0f) where T : IAudioResource => resource.IsValid ? PlayOneShot(resource.ID, volumeScale) : AudioHandle.None;

        // A value of a parameter of the sound events it plays, by its name, kept for its later plays too, as Unity 6's AudioSource keeps its Audio Random Container's state, so a footstep's surface is set once and each step picks by it. A Switch reads it as it is triggered, and a continuous one is kept within its range, every instance playing seeking towards it at its speed. A global one is Audio's, which this leaves as it is
        public void SetParameter(string name, float value)
        {
            int l_Result;
            using (Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]))
            {
                fixed (byte* l_Data = l_Name.Bytes)
                {
                    l_Result = Native.AudioSource_SetParameter(m_Entity.ID, new Utf8(l_Data, l_Name.Bytes.Length), value);
                }
            }

            Check(l_Result);
        }

        // By one of the parameter's labels, as the sound event it names has them once it has loaded. False before then, or for a label it lacks
        public bool SetParameterLabel(string name, string label)
        {
            int l_Result;
            bool l_Found;
            using (Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]))
            using (Utf8String l_Label = new(label, stackalloc byte[Utf8String.c_StackBytes]))
            {
                fixed (byte* l_NameData = l_Name.Bytes)
                fixed (byte* l_LabelData = l_Label.Bytes)
                {
                    l_Result = Native.AudioSource_SetParameterLabel(m_Entity.ID, new Utf8(l_NameData, l_Name.Bytes.Length), new Utf8(l_LabelData, l_Label.Bytes.Length), &l_Found);
                }
            }

            Check(l_Result);

            return l_Found;
        }

        // The value it was given, or else the default of the sound event it plays. False, and 0, for neither
        public bool GetParameter(string name, out float value)
        {
            int l_Result;
            float l_Value = 0.0f;
            bool l_Found;
            using (Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]))
            {
                fixed (byte* l_Data = l_Name.Bytes)
                {
                    l_Result = Native.AudioSource_GetParameter(m_Entity.ID, new Utf8(l_Data, l_Name.Bytes.Length), &l_Value, &l_Found);
                }
            }

            Check(l_Result);
            value = l_Value;

            return l_Found;
        }

        // The group of the project's mixer it plays into, as Unity's outputAudioMixerGroup, None for Master. Changed while it plays, its sound moves there as the scene's sounds next update
        public AudioMixerGroup Output
        {
            get
            {
                ulong l_Group;
                Check(Native.AudioSource_GetOutput(m_Entity.ID, &l_Group));

                return new AudioMixerGroup(l_Group);
            }
            set => Check(Native.AudioSource_SetOutput(m_Entity.ID, value.ID));
        }

        private AudioHandle PlayOneShot(ulong resource, float volumeScale)
        {
            ulong l_Handle;
            Check(Native.AudioSource_PlayOneShot(m_Entity.ID, resource, volumeScale, &l_Handle));

            return new AudioHandle(l_Handle);
        }

        // The message made only for a failure, so a call that works allocates nothing
        private void Check(int result)
        {
            if ((AccessResult)result != AccessResult.Done)
            {
                ComponentAccess.Check(m_Entity, (AccessResult)result, c_NoSource);
            }
        }
    }
}