#pragma once

#include <memory>

#ifdef TR_PLATFORM_WINDOWS
#if TR_DYNAMIC_BUILD
        #ifdef TR_BUILD_DLL
               #define __declspec(dllexport)
        #else
               #define __declspec(dllimport)
        #endif
#else
        #define TRINITY_API
#endif
#else
        #error Trinity only supports windows!
#endif

#ifdef TR_DEBUG
        #define TR_ENABLE_ASSERTS
#endif

#ifdef TR_ENABLE_ASSERTS
        #define TR_ASSERT(x, ...) { if(!(x)) { TR_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
        #define TR_CORE_ASSERT(x, ...) { if(!(x)) { TR_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#else
        #define TR_ASSERT(x, ...)
        #define TR_CORE_ASSERT(x, ...)
#endif

//--------------------DEFINED FUNCTIONS-------------------------------------
#define BIT(x) (1 << x)
#define TR_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }
//--------------------------------------------------------------------------


namespace Trinity
{
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