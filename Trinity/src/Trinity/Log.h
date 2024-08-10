#pragma once

#include "trpch.h"

#include "Core.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/fmt/ostr.h"

namespace Trinity
{
	class TRINITY_API Log
	{
	public:
		static void Init();

		inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;
	};
}

// Core log macros
#define TR_CORE_TRACE(...)   ::Trinity::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define TR_CORE_INFO(...)    ::Trinity::Log::GetCoreLogger()->info(__VA_ARGS__)
#define TR_CORE_WARN(...)    ::Trinity::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define TR_CORE_ERROR(...)   ::Trinity::Log::GetCoreLogger()->error(__VA_ARGS__)
#define TR_CORE_FATAL(...)   ::Trinity::Log::GetCoreLogger()->fatal(__VA_ARGS__)

// Client log macros
#define TR_CLIENT_TRACE(...)   ::Trinity::Log::GetClientLogger()->trace(__VA_ARGS__)
#define TR_CLIENT_INFO(...)    ::Trinity::Log::GetClientLogger()->info(__VA_ARGS__)
#define TR_CLIENT_WARN(...)    ::Trinity::Log::GetClientLogger()->warn(__VA_ARGS__)
#define TR_CLIENT_ERROR(...)   ::Trinity::Log::GetClientLogger()->error(__VA_ARGS__)
#define TR_CLIENT_FATAL(...)   ::Trinity::Log::GetClientLogger()->fatal(__VA_ARGS__)