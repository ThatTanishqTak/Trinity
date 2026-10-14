using Trinity.Interop;

namespace Trinity
{
    // The running scene's clock: this frame's time, which OnUpdate is given, the length of a fixed step, which OnFixedUpdate is given, the scene's time since it started running, in seconds, and the frames and fixed steps it has run, each counted as it begins. Step in play mode is one frame of one step's time, with one fixed step. All 0 while no scene runs
    public static class Time
    {
        public static float DeltaTime => Native.Time_GetDeltaTime();
        public static float FixedDeltaTime => Native.Time_GetFixedDeltaTime();
        public static double Elapsed => Native.Time_GetElapsed();
        public static ulong FrameCount => Native.Time_GetFrameCount();
        public static ulong StepCount => Native.Time_GetStepCount();
    }
}