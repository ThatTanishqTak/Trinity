#pragma once

#include "Trinity/Core/Base.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
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

    // As the console and the log file name them
    [[nodiscard]] TRINITY_API std::string_view ToString(LogChannel channel);
    [[nodiscard]] TRINITY_API std::string_view ToString(LogLevel level);

    class TRINITY_API Log
    {
    public:
        static void Initialize(const std::filesystem::path& logFile);
        static void Shutdown();

        [[nodiscard]] static bool ShouldLog(LogChannel channel, LogLevel level);
        static void Write(LogChannel channel, LogLevel level, std::string_view message);
        static void Flush();

        // Local time as HH:MM:SS.mmm, as the console shows it
        [[nodiscard]] static std::string FormatTime(std::chrono::system_clock::time_point time);

        template<typename... Args>
        static void Print(LogChannel channel, LogLevel level, std::format_string<Args...> format, Args&&... args)
        {
            if (ShouldLog(channel, level))
            {
                Write(channel, level, std::format(format, std::forward<Args>(args)...));
            }
        }
    };

    struct LogEntry
    {
        std::uint64_t Sequence = 0;
        std::chrono::system_clock::time_point Time;
        LogChannel Channel = LogChannel::Core;
        LogLevel Level = LogLevel::Info;
        std::string_view Text;
    };

    // The last lines written on any thread, in the order they were written. Its memory is set aside once, under the Log tag, and the oldest lines drop out as it fills
    class TRINITY_API LogHistory
    {
    public:
        static constexpr std::size_t c_LineCapacity = 4096;
        static constexpr std::size_t c_TextCapacity = 1024 * 1024;
        static constexpr std::size_t c_MaximumLineSize = 8 * 1024;

        // Holds the history's lock while it lives, so lines written meanwhile wait, and every entry's text stays valid
        class TRINITY_API Reader
        {
        public:
            [[nodiscard]] std::size_t GetCount() const;
            [[nodiscard]] const LogEntry& operator[](std::size_t index) const;

        private:
            friend class LogHistory;

            explicit Reader(std::unique_lock<std::mutex> lock);

            std::unique_lock<std::mutex> m_Lock;
        };

        static void Initialize();
        static void Shutdown();

        [[nodiscard]] static Reader Read();
        [[nodiscard]] static std::uint64_t GetNextSequence();
        static void Clear();

        static void Add(LogChannel channel, LogLevel level, std::chrono::system_clock::time_point time, std::string_view text);
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