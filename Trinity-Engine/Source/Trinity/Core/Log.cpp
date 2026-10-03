#include "Trinity/Core/Log.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <array>
#include <system_error>
#include <vector>

namespace Trinity
{
    namespace
    {
        std::array<std::shared_ptr<spdlog::logger>, 2> s_Loggers;

        spdlog::logger* GetLogger(LogChannel channel)
        {
            return s_Loggers[static_cast<std::size_t>(channel)].get();
        }

        spdlog::level::level_enum ToSpdlogLevel(LogLevel level)
        {
            switch (level)
            {
                case LogLevel::Trace:
                {
                    return spdlog::level::trace;
                }
                case LogLevel::Info:
                {
                    return spdlog::level::info;
                }
                case LogLevel::Warn:
                {
                    return spdlog::level::warn;
                }
                case LogLevel::Error:
                {
                    return spdlog::level::err;
                }
                case LogLevel::Critical:
                {
                    return spdlog::level::critical;
                }
            }

            return spdlog::level::critical;
        }
    }

    void Log::Initialize(const std::filesystem::path& logFile)
    {
        std::vector<spdlog::sink_ptr> l_Sinks;

        auto a_ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        a_ConsoleSink->set_pattern("%^[%T.%e] %-7n %-8l%$ %v");
        l_Sinks.push_back(std::move(a_ConsoleSink));

        std::string l_FileSinkError;
        try
        {
            if (logFile.has_parent_path())
            {
                std::error_code l_Ignored;
                std::filesystem::create_directories(logFile.parent_path(), l_Ignored);
            }

            auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile.string(), true);
            fileSink->set_pattern("[%Y-%m-%d %T.%e] %-7n %-8l %v");
            l_Sinks.push_back(std::move(fileSink));
        }
        catch (const spdlog::spdlog_ex& exception)
        {
            l_FileSinkError = exception.what();
        }

        s_Loggers[static_cast<std::size_t>(LogChannel::Core)] = std::make_shared<spdlog::logger>("TRINITY", l_Sinks.begin(), l_Sinks.end());
        s_Loggers[static_cast<std::size_t>(LogChannel::Client)] = std::make_shared<spdlog::logger>("APP", l_Sinks.begin(), l_Sinks.end());

        for (const std::shared_ptr<spdlog::logger>& it_Logger : s_Loggers)
        {
            it_Logger->set_level(spdlog::level::trace);
            it_Logger->flush_on(spdlog::level::warn);
            spdlog::register_logger(it_Logger);
        }

        if (!l_FileSinkError.empty())
        {
            TR_CORE_WARN("File logging is disabled: {}", l_FileSinkError);
        }
    }

    void Log::Shutdown()
    {
        for (std::shared_ptr<spdlog::logger>& it_Logger : s_Loggers)
        {
            it_Logger.reset();
        }

        spdlog::shutdown();
    }

    bool Log::ShouldLog(LogChannel channel, LogLevel level)
    {
        const spdlog::logger* l_Logger = GetLogger(channel);

        return l_Logger != nullptr && l_Logger->should_log(ToSpdlogLevel(level));
    }

    void Log::Write(LogChannel channel, LogLevel level, std::string_view message)
    {
        if (spdlog::logger* l_Logger = GetLogger(channel))
        {
            l_Logger->log(ToSpdlogLevel(level), message);
        }
    }

    void Log::Flush()
    {
        for (const std::shared_ptr<spdlog::logger>& it_Logger : s_Loggers)
        {
            if (it_Logger != nullptr)
            {
                it_Logger->flush();
            }
        }
    }
}