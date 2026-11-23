using Trinity.Interop;

namespace Trinity
{
    // As Unity's AudioSource: Play, Stop, Pause, UnPause and PlayOneShot. With no AudioSource it throws
    public readonly unsafe partial struct AudioSource
    {
        private const string c_NoSource = "has no AudioSource";

        public void Play() => Check(Native.AudioSource_Play(m_Entity.ID));

        public void Stop() => Check(Native.AudioSource_Stop(m_Entity.ID));

        public void Pause() => Check(Native.AudioSource_Pause(m_Entity.ID));

        public void UnPause() => Check(Native.AudioSource_UnPause(m_Entity.ID));

        // From Play until what it plays ends or is stopped, its wait for its clip too. Not while paused
        public bool IsPlaying
        {
            get
            {
                bool l_Playing;
                Check(Native.AudioSource_IsPlaying(m_Entity.ID, &l_Playing));

                return l_Playing;
            }
        }

        // Where it is in its clip, or how long its sound event has played, in seconds, 0 if nothing plays
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

        // Its own Resource beside what it plays, sharing its parameters. None for a source with no Resource
        public AudioHandle PlayOneShot(float volumeScale = 1.0f) => PlayOneShot(0, volumeScale);

        // A clip or a sound event in the same way. None for a resource that is None
        public AudioHandle PlayOneShot<T>(AssetRef<T> resource, float volumeScale = 1.0f) where T : IAudioResource => resource.IsValid ? PlayOneShot(resource.ID, volumeScale) : AudioHandle.None;

        // A parameter of the sound events it plays, by name, kept for its later plays. Not a global one
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

        // By one of the parameter's labels, once its sound event has loaded. False before, or if unknown
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

        // The value it was given, or else its sound event's default. False, and 0, for neither
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

        // The mixer group it plays into, as Unity's outputAudioMixerGroup. None is Master
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