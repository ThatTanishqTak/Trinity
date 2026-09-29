#pragma once

#include "Trinity/Core/Log.hpp"

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace Trinity::Detail
{
	template<typename... Args>
	void ReportCheckFailure(const std::shared_ptr<spdlog::logger>& logger, spdlog::level::level_enum level, std::string_view kind, std::string_view condition, const char* file, int line, std::format_string<Args...> message, Args&&... args)
	{
		if (!logger)
		{
			return;
		}

		const std::string l_Message = std::format(message, std::forward<Args>(args)...);
		if (l_Message.empty())
		{
			logger->log(level, "{} failed: {} ({}:{})", kind, condition, file, line);
		}
		else
		{
			logger->log(level, "{} failed: {} ({}:{}): {}", kind, condition, file, line, l_Message);
		}
	}
}

#if defined(TR_ENABLE_ASSERTS)

#if defined(_MSC_VER)
#define TR_DEBUGBREAK() __debugbreak()
#elif defined(__linux__) || defined(__APPLE__)
#include <csignal>
#define TR_DEBUGBREAK() std::raise(SIGTRAP)
#else
#define TR_DEBUGBREAK() __builtin_trap()
#endif

#define TR_CORE_ASSERT(condition, ...) \
	do \
	{ \
		if (!(condition)) \
		{ \
			::Trinity::Detail::ReportCheckFailure(::Trinity::Log::GetCoreLogger(), ::spdlog::level::critical, "Assertion", #condition, __FILE__, __LINE__, "" __VA_ARGS__); \
			TR_DEBUGBREAK(); \
		} \
	} while (false)

#define TR_ASSERT(condition, ...) \
	do \
	{ \
		if (!(condition)) \
		{ \
			::Trinity::Detail::ReportCheckFailure(::Trinity::Log::GetClientLogger(), ::spdlog::level::critical, "Assertion", #condition, __FILE__, __LINE__, "" __VA_ARGS__); \
			TR_DEBUGBREAK(); \
		} \
	} while (false)

#define TR_CORE_VERIFY(condition, ...) \
	(static_cast<bool>(condition) || (::Trinity::Detail::ReportCheckFailure(::Trinity::Log::GetCoreLogger(), ::spdlog::level::critical, "Verify", #condition, __FILE__, __LINE__, "" __VA_ARGS__), TR_DEBUGBREAK(), false))

#define TR_VERIFY(condition, ...) \
	(static_cast<bool>(condition) || (::Trinity::Detail::ReportCheckFailure(::Trinity::Log::GetClientLogger(), ::spdlog::level::critical, "Verify", #condition, __FILE__, __LINE__, "" __VA_ARGS__), TR_DEBUGBREAK(), false))

#else

#define TR_CORE_ASSERT(condition, ...) ((void)sizeof(!(condition)))
#define TR_ASSERT(condition, ...) ((void)sizeof(!(condition)))

#define TR_CORE_VERIFY(condition, ...) \
	(static_cast<bool>(condition) || (::Trinity::Detail::ReportCheckFailure(::Trinity::Log::GetCoreLogger(), ::spdlog::level::err, "Verify", #condition, __FILE__, __LINE__, "" __VA_ARGS__), false))

#define TR_VERIFY(condition, ...) \
	(static_cast<bool>(condition) || (::Trinity::Detail::ReportCheckFailure(::Trinity::Log::GetClientLogger(), ::spdlog::level::err, "Verify", #condition, __FILE__, __LINE__, "" __VA_ARGS__), false))

#endif