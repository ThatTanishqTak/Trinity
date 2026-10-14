#include "Trinity/Scripting/Interop.hpp"

#include "Trinity/Core/Log.hpp"

namespace Trinity::Interop
{
    // A level C# does not have is logged as an error, so nothing a script writes is lost
    Void Log_Write(LogLevel level, Utf8 text)
    {
        const LogLevel l_Level = level <= LogLevel::Critical ? level : LogLevel::Error;
        Log::Print(LogChannel::Client, l_Level, "{}", ToStringView(text));
    }

    Int32 Interop_Echo(Int32 value)
    {
        return value + 1;
    }
}