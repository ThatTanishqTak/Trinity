#pragma once

#include "Trinity/Core/Log.hpp"

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
			TR_CORE_CRITICAL("Assertion failed: {} ({}:{})", #condition, __FILE__, __LINE__); \
			TR_CORE_CRITICAL(__VA_ARGS__); \
			TR_DEBUGBREAK(); \
		} \
	} while (false)

#define TR_ASSERT(condition, ...) \
	do \
	{ \
		if (!(condition)) \
		{ \
			TR_CRITICAL("Assertion failed: {} ({}:{})", #condition, __FILE__, __LINE__); \
			TR_CRITICAL(__VA_ARGS__); \
			TR_DEBUGBREAK(); \
		} \
	} while (false)

#else

#define TR_CORE_ASSERT(condition, ...) ((void)0)
#define TR_ASSERT(condition, ...) ((void)0)

#endif