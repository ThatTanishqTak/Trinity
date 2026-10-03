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