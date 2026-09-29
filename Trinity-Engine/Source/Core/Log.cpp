#include "Trinity/Core/Log.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <vector>

namespace Trinity
{
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

	namespace
	{
		constexpr std::chrono::seconds s_FlushInterval{ 3 };

		std::shared_ptr<spdlog::logger> CreateLogger(const char* name, const std::vector<spdlog::sink_ptr>& sinks)
		{
			std::shared_ptr<spdlog::logger> l_Logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
			spdlog::register_logger(l_Logger);
			l_Logger->set_level(spdlog::level::trace);

			// Warnings and errors reach the file immediately, everything else is flushed every few seconds
			l_Logger->flush_on(spdlog::level::warn);

			return l_Logger;
		}
	}

	void Log::Initialize()
	{
		std::vector<spdlog::sink_ptr> l_LogSinks;
		l_LogSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
		l_LogSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("Trinity.log", true));

		l_LogSinks[0]->set_pattern("%^[%T] %n: %v%$"); // Console
		l_LogSinks[1]->set_pattern("[%T] [%l] %n: %v"); // File

		s_CoreLogger = CreateLogger("TRINITY", l_LogSinks);
		s_ClientLogger = CreateLogger("APP", l_LogSinks);

		spdlog::flush_every(s_FlushInterval);
	}

	void Log::Shutdown()
	{
		s_ClientLogger.reset();
		s_CoreLogger.reset();

		// Stops the flush thread, then flushes and drops every registered logger, which closes the file sink
		spdlog::shutdown();
	}
}