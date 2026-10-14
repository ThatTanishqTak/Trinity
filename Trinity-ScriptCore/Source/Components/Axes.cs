using System;

namespace Trinity
{
    // World axes, as a body's locked position and rotation name them
    [Flags]
    public enum Axes
    {
        None = 0,
        X = 1 << 0,
        Y = 1 << 1,
        Z = 1 << 2,
        All = X | Y | Z
    }
}