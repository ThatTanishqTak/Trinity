using Trinity.Interop;

namespace Trinity
{
    // The running scene's clock: this frame's time, which OnUpdate is given, the length of a fixed step, which OnFixedUpdate is given, the scene's time since it started running, in seconds, and the frames and fixed steps it has run, each counted as it begins. Step in play mode is one frame of one step's time, with one fixed step. The frame's time and the scene's are scaled by TimeScale, as Unity's are, and the unscaled ones are as the frames came, for what goes on while the game is paused, such as its menus. All 0 while no scene runs, but TimeScale, which is 1
    public static class Time
    {
        // As Unity's, the most TimeScale may be
        public const float MaxTimeScale = 100.0f;

        public static float DeltaTime => Native.Time_GetDeltaTime();
        public static float FixedDeltaTime => Native.Time_GetFixedDeltaTime();
        public static double Elapsed => Native.Time_GetElapsed();
        public static ulong FrameCount => Native.Time_GetFrameCount();
        public static ulong StepCount => Native.Time_GetStepCount();
        public static float UnscaledDeltaTime => Native.Time_GetUnscaledDeltaTime();
        public static double UnscaledElapsed => Native.Time_GetUnscaledElapsed();

        // As Unity's Time.timeScale: how fast the running scene's clock goes from the next frame on, 1 at its own pace, 0 stopped, and up to MaxTimeScale. Stopped, no fixed step is taken, so physics, OnFixedUpdate, timers and coroutines waiting on seconds hold where they are, and OnUpdate goes on with a DeltaTime of 0, for the game's menus and input, while the UI and sounds go on too. Kept as the scene switches, as Unity's is. A value outside 0 to MaxTimeScale, or not a number, is refused and logged, and the scale stays as it was
        public static float TimeScale
        {
            get => Native.Time_GetTimeScale();
            set => Native.Time_SetTimeScale(value);
        }
    }
}