#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#if !defined(TR_PLATFORM_WINDOWS) && !defined(TR_PLATFORM_LINUX) && !defined(TR_PLATFORM_MACOS) && \
    !defined(TR_PLATFORM_XBOX) && !defined(TR_PLATFORM_PLAYSTATION) && !defined(TR_PLATFORM_SWITCH)
    #error "No TR_PLATFORM_* macro is defined. Trinity must be built through its CMake project."
#endif

#if !defined(TR_DEBUG) && !defined(TR_RELEASE) && !defined(TR_DISTRIBUTION)
    #error "No configuration macro is defined. Expected one of TR_DEBUG, TR_RELEASE, TR_DISTRIBUTION."
#endif

#if defined(_MSC_VER)
    #define TR_DEBUGBREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define TR_DEBUGBREAK() __builtin_trap()
#else
    #error "TR_DEBUGBREAK is not implemented for this compiler."
#endif

#define TR_BIT(x) (1u << (x))

#define TR_BIND_EVENT_FN(fn) \
    [this](auto&&... eventArgs) -> decltype(auto) { return this->fn(std::forward<decltype(eventArgs)>(eventArgs)...); }

namespace Trinity
{
    template<typename T>
    using Scope = std::unique_ptr<T>;

    template<typename T, typename... Args>
    [[nodiscard]] constexpr Scope<T> CreateScope(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    using Ref = std::shared_ptr<T>;

    template<typename T, typename... Args>
    [[nodiscard]] Ref<T> CreateRef(Args&&... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    [[nodiscard]] const char* GetVersionString();
}