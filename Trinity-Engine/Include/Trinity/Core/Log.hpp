#pragma once

#include "Trinity/Core/Base.hpp"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>

namespace Trinity
{
    class Log
    {
    public:
        static void Initialize(const std::filesystem::path& logFile);
        static void Shutdown();

        [[nodiscard]] static spdlog::logger& Core() { return *s_CoreLogger; }
        [[nodiscard]] static spdlog::logger& Client() { return *s_ClientLogger; }

    private:
        static std::shared_ptr<spdlog::logger> s_CoreLogger;
        static std::shared_ptr<spdlog::logger> s_ClientLogger;
    };
}

// Engine-side logging.
#define TR_CORE_WARN(...) ::Trinity::Log::Core().warn(__VA_ARGS__)
#define TR_CORE_ERROR(...) ::Trinity::Log::Core().error(__VA_ARGS__)
#define TR_CORE_CRITICAL(...) ::Trinity::Log::Core().critical(__VA_ARGS__)

// Application-side logging.
#define TR_WARN(...) ::Trinity::Log::Client().warn(__VA_ARGS__)
#define TR_ERROR(...) ::Trinity::Log::Client().error(__VA_ARGS__)
#define TR_CRITICAL(...) ::Trinity::Log::Client().critical(__VA_ARGS__)

#if defined(TR_DIST)
    #define TR_INTERNAL_LOG_DISABLED(call) do { if (false) { call; } } while (false)

    #define TR_CORE_TRACE(...) TR_INTERNAL_LOG_DISABLED(::Trinity::Log::Core().trace(__VA_ARGS__))
    #define TR_CORE_INFO(...) TR_INTERNAL_LOG_DISABLED(::Trinity::Log::Core().info(__VA_ARGS__))
    #define TR_TRACE(...) TR_INTERNAL_LOG_DISABLED(::Trinity::Log::Client().trace(__VA_ARGS__))
    #define TR_INFO(...) TR_INTERNAL_LOG_DISABLED(::Trinity::Log::Client().info(__VA_ARGS__))
#else
    #define TR_CORE_TRACE(...) ::Trinity::Log::Core().trace(__VA_ARGS__)
    #define TR_CORE_INFO(...) ::Trinity::Log::Core().info(__VA_ARGS__)
    #define TR_TRACE(...) ::Trinity::Log::Client().trace(__VA_ARGS__)
    #define TR_INFO(...) ::Trinity::Log::Client().info(__VA_ARGS__)
#endif