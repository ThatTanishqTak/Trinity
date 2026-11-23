using System;

namespace Trinity
{
    // What a coroutine yields to wait, on the scene's clock. Or it yields an IEnumerator or Coroutine
    public abstract class YieldInstruction
    {
        private protected YieldInstruction() { }
    }

    public sealed class WaitForSeconds : YieldInstruction
    {
        public float Seconds { get; }

        public WaitForSeconds(float seconds)
        {
            Seconds = seconds;
        }
    }

    // At least 1
    public sealed class WaitForFrames : YieldInstruction
    {
        public int Frames { get; }

        public WaitForFrames(int frames)
        {
            Frames = Math.Max(frames, 1);
        }
    }

    public sealed class WaitForFixedUpdate : YieldInstruction
    {
    }

    public sealed class WaitUntil : YieldInstruction
    {
        public Func<bool> Condition { get; }

        public WaitUntil(Func<bool> condition)
        {
            Condition = condition ?? throw new ArgumentNullException(nameof(condition));
        }
    }

    public sealed class WaitWhile : YieldInstruction
    {
        public Func<bool> Condition { get; }

        public WaitWhile(Func<bool> condition)
        {
            Condition = condition ?? throw new ArgumentNullException(nameof(condition));
        }
    }

    // Stays valid to stop or ask about after it has gone. Default if never started or finished at once
    public readonly struct Coroutine : IEquatable<Coroutine>
    {
        public ulong ID { get; }

        internal Coroutine(ulong id)
        {
            ID = id;
        }

        public bool Equals(Coroutine other) => ID == other.ID;
        public override bool Equals(object? obj) => obj is Coroutine other && Equals(other);
        public override int GetHashCode() => ID.GetHashCode();
    }

    public readonly struct Timer : IEquatable<Timer>
    {
        public ulong ID { get; }

        internal Timer(ulong id)
        {
            ID = id;
        }

        public bool Equals(Timer other) => ID == other.ID;
        public override bool Equals(object? obj) => obj is Timer other && Equals(other);
        public override int GetHashCode() => ID.GetHashCode();
    }
}