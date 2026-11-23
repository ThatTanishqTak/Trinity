using Trinity.Interop;

namespace Trinity
{
    // As Unity's Random: the engine's one generator, so a seed gives the same numbers everywhere
    public static class Random
    {
        public static void InitState(int seed) => Native.Random_InitState(seed);

        // From 0 to 1, both of them included, as Unity's value is
        public static float Value => Native.Random_GetValue();

        // From min to max, both of them included, the two swapped when max is below min
        public static float Range(float min, float max) => Native.Random_RangeFloat(min, max);

        // From min up to but not including max, each as likely, or min itself when max is not above it
        public static int Range(int min, int max) => Native.Random_RangeInt(min, max);
    }
}