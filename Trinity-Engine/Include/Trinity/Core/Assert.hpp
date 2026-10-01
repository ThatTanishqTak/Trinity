#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Log.hpp"

#define TR_INTERNAL_ASSERT_FAILED(logger, condition, ...)                                           \
    do                                                                                              \
    {                                                                                               \
        logger("Assertion failed: {}  ({}:{})", #condition, __FILE__, __LINE__);                    \
        __VA_OPT__(logger(__VA_ARGS__);)                                                            \
        TR_DEBUGBREAK();                                                                            \
    } while (false)

#if defined(TR_ENABLE_ASSERTS)
    #define TR_CORE_ASSERT(condition, ...) \
        do { if (!(condition)) [[unlikely]] { TR_INTERNAL_ASSERT_FAILED(TR_CORE_CRITICAL, condition __VA_OPT__(,) __VA_ARGS__); } } while (false)
    #define TR_ASSERT(condition, ...) \
        do { if (!(condition)) [[unlikely]] { TR_INTERNAL_ASSERT_FAILED(TR_CRITICAL, condition __VA_OPT__(,) __VA_ARGS__); } } while (false)
    #define TR_CORE_VERIFY(condition, ...) TR_CORE_ASSERT(condition __VA_OPT__(,) __VA_ARGS__)
    #define TR_VERIFY(condition, ...) TR_ASSERT(condition __VA_OPT__(,) __VA_ARGS__)
#else
    #define TR_CORE_ASSERT(condition, ...) ((void)0)
    #define TR_ASSERT(condition, ...) ((void)0)
    #define TR_CORE_VERIFY(condition, ...) ((void)(condition))
    #define TR_VERIFY(condition, ...) ((void)(condition))
#endif