#include "Trinity/Core/Log.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <system_error>
#include <vector>

namespace Trinity
{
    std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
    std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

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

        s_CoreLogger = std::make_shared<spdlog::logger>("TRINITY", l_Sinks.begin(), l_Sinks.end());
        s_ClientLogger = std::make_shared<spdlog::logger>("APP", l_Sinks.begin(), l_Sinks.end());

        for (const auto& it_Logger : { s_CoreLogger, s_ClientLogger })
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
        s_ClientLogger.reset();
        s_CoreLogger.reset();
        spdlog::shutdown();
    }
}