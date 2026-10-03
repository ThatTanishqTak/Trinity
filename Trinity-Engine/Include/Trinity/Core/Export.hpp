#pragma once

#if defined(TR_ENGINE_SHARED)
#if defined(_WIN32)
#if defined(TR_BUILD_ENGINE)
#define TRINITY_API __declspec(dllexport)
#else
#define TRINITY_API __declspec(dllimport)
#endif
#else
#define TRINITY_API __attribute__((visibility("default")))
#endif
#else
#define TRINITY_API
#endif

// The C entry points a module exports for the executable that loads it
#if defined(_WIN32)
#define TRINITY_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define TRINITY_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif