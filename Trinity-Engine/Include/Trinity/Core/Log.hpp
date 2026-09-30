#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace Trinity
{
	class Log
	{
	public:
		static void Initialize();
		static void Shutdown();

		static std::shared_ptr<spdlog::logger>& GetCoreLogger();
		static std::shared_ptr<spdlog::logger>& GetClientLogger();
	};

}

// Core log macros
#define TR_CORE_TRACE(...) SPDLOG_LOGGER_TRACE(::Trinity::Log::GetCoreLogger(), __VA_ARGS__)
#define TR_CORE_INFO(...) SPDLOG_LOGGER_INFO(::Trinity::Log::GetCoreLogger(), __VA_ARGS__)
#define TR_CORE_WARN(...) SPDLOG_LOGGER_WARN(::Trinity::Log::GetCoreLogger(), __VA_ARGS__)
#define TR_CORE_ERROR(...) SPDLOG_LOGGER_ERROR(::Trinity::Log::GetCoreLogger(), __VA_ARGS__)
#define TR_CORE_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::Trinity::Log::GetCoreLogger(), __VA_ARGS__)

// Client log macros
#define TR_TRACE(...) SPDLOG_LOGGER_TRACE(::Trinity::Log::GetClientLogger(), __VA_ARGS__)
#define TR_INFO(...) SPDLOG_LOGGER_INFO(::Trinity::Log::GetClientLogger(), __VA_ARGS__)
#define TR_WARN(...) SPDLOG_LOGGER_WARN(::Trinity::Log::GetClientLogger(), __VA_ARGS__)
#define TR_ERROR(...) SPDLOG_LOGGER_ERROR(::Trinity::Log::GetClientLogger(), __VA_ARGS__)
#define TR_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::Trinity::Log::GetClientLogger(), __VA_ARGS__)