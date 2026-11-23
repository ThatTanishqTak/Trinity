using Trinity.Interop;

namespace Trinity
{
    // The running scene's clock, scaled by TimeScale, as Unity's. All 0 with no scene, but TimeScale
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

        // As Unity's Time.timeScale, from the next frame on. Outside 0 to MaxTimeScale is refused, logged
        public static float TimeScale
        {
            get => Native.Time_GetTimeScale();
            set => Native.Time_SetTimeScale(value);
        }
    }
}