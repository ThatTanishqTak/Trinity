using Trinity.Interop;

namespace Trinity
{
    // The engine's log, on the channel of the game's own lines
    public static class Log
    {
        public static void Trace(string message) => NativeLog.Write(LogLevel.Trace, message);
        public static void Info(string message) => NativeLog.Write(LogLevel.Info, message);
        public static void Warn(string message) => NativeLog.Write(LogLevel.Warn, message);
        public static void Error(string message) => NativeLog.Write(LogLevel.Error, message);
        public static void Critical(string message) => NativeLog.Write(LogLevel.Critical, message);
    }
}