using System;

namespace Trinity
{
    // A group of the project's mixer, by its ID, as Unity's AudioMixerGroup, which AudioMixer.FindGroup finds by its name and an AudioSource's Output plays into. None plays into Master, as does a group the mixer no longer has
    public readonly struct AudioMixerGroup : IEquatable<AudioMixerGroup>
    {
        public static AudioMixerGroup None => default;

        public ulong ID { get; }

        public bool IsValid => ID != 0;

        public AudioMixerGroup(ulong id)
        {
            ID = id;
        }

        public bool Equals(AudioMixerGroup other) => ID == other.ID;
        public override bool Equals(object? obj) => obj is AudioMixerGroup other && Equals(other);
        public override int GetHashCode() => ID.GetHashCode();
        public override string ToString() => ID.ToString("x16");

        public static bool operator ==(AudioMixerGroup left, AudioMixerGroup right) => left.Equals(right);
        public static bool operator !=(AudioMixerGroup left, AudioMixerGroup right) => !left.Equals(right);
    }
}