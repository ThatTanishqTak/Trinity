#pragma once

#include "Trinity/Core/PlatformDetection.h"

#include <memory>

#ifdef TR_DEBUG
	   #if defined(TR_PLATFORM_WINDOWS)
			   #define TR_DEBUGBREAK() __debugbreak()
	   #elif defined(TR_PLATFORM_LINUX)
			   #include <signal.h>
			   #define TR_DEBUGBREAK() raise(SIGTRAP)
	   #else
			   #error "Platform doesn't support debugbreak yet!"
	   #endif
			   #define TR_ENABLE_ASSERTS
	   #else
			#define TR_DEBUGBREAK()
	   #endif

#define TR_EXPAND_MACRO(x) x
#define TR_STRINGIFY_MACRO(x) #x

#define BIT(x) (1 << x)

#define TR_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

namespace Trinity {

	template<typename T>
	using Scope = std::unique_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Scope<T> CreateScope(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Ref<T> CreateRef(Args&& ... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

}

#include "Trinity/Core/Log.h"
#include "Trinity/Core/Assert.h"