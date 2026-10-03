#pragma once

#include "Trinity/Core/Base.hpp"

#include <cstdint>
#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace Trinity
{
    enum class LogChannel : std::uint8_t
    {
        Core,
        Client
    };

    enum class LogLevel : std::uint8_t
    {
        Trace,
        Info,
        Warn,
        Error,
        Critical
    };

    class TRINITY_API Log
    {
    public:
        static void Initialize(const std::filesystem::path& logFile);
        static void Shutdown();

        [[nodiscard]] static bool ShouldLog(LogChannel channel, LogLevel level);
        static void Write(LogChannel channel, LogLevel level, std::string_view message);
        static void Flush();

        template<typename... Args>
        static void Print(LogChannel channel, LogLevel level, std::format_string<Args...> format, Args&&... args)
        {
            if (ShouldLog(channel, level))
            {
                Write(channel, level, std::format(format, std::forward<Args>(args)...));
            }
        }
    };
}

// Engine-side logging.
#define TR_CORE_WARN(...) ::Trinity::Log::Print(::Trinity::LogChannel::Core, ::Trinity::LogLevel::Warn, __VA_ARGS__)
#define TR_CORE_ERROR(...) ::Trinity::Log::Print(::Trinity::LogChannel::Core, ::Trinity::LogLevel::Error, __VA_ARGS__)
#define TR_CORE_CRITICAL(...) ::Trinity::Log::Print(::Trinity::LogChannel::Core, ::Trinity::LogLevel::Critical, __VA_ARGS__)

// Application-side logging.
#define TR_WARN(...) ::Trinity::Log::Print(::Trinity::LogChannel::Client, ::Trinity::LogLevel::Warn, __VA_ARGS__)
#define TR_ERROR(...) ::Trinity::Log::Print(::Trinity::LogChannel::Client, ::Trinity::LogLevel::Error, __VA_ARGS__)
#define TR_CRITICAL(...) ::Trinity::Log::Print(::Trinity::LogChannel::Client, ::Trinity::LogLevel::Critical, __VA_ARGS__)

#if defined(TR_DISTRIBUTION)
#define TR_INTERNAL_LOG_DISABLED(call) do { if (false) { call; } } while (false)

#define TR_CORE_TRACE(...) TR_INTERNAL_LOG_DISABLED(::Trinity::Log::Print(::Trinity::LogChannel::Core, ::Trinity::LogLevel::Trace, __VA_ARGS__))
#define TR_CORE_INFO(...) TR_INTERNAL_LOG_DISABLED(::Trinity::Log::Print(::Trinity::LogChannel::Core, ::Trinity::LogLevel::Info, __VA_ARGS__))
#define TR_TRACE(...) TR_INTERNAL_LOG_DISABLED(::Trinity::Log::Print(::Trinity::LogChannel::Client, ::Trinity::LogLevel::Trace, __VA_ARGS__))
#define TR_INFO(...) TR_INTERNAL_LOG_DISABLED(::Trinity::Log::Print(::Trinity::LogChannel::Client, ::Trinity::LogLevel::Info, __VA_ARGS__))
#else
#define TR_CORE_TRACE(...) ::Trinity::Log::Print(::Trinity::LogChannel::Core, ::Trinity::LogLevel::Trace, __VA_ARGS__)
#define TR_CORE_INFO(...) ::Trinity::Log::Print(::Trinity::LogChannel::Core, ::Trinity::LogLevel::Info, __VA_ARGS__)
#define TR_TRACE(...) ::Trinity::Log::Print(::Trinity::LogChannel::Client, ::Trinity::LogLevel::Trace, __VA_ARGS__)
#define TR_INFO(...) ::Trinity::Log::Print(::Trinity::LogChannel::Client, ::Trinity::LogLevel::Info, __VA_ARGS__)
#endif