#pragma once

#ifdef TR_PLATFORM_WINDOWS
        #ifdef TR_BUILD_DLL
               #define TRINITY_API __declspec(dllexport)
        #else
               #define TRINITY_API __declspec(dllimport)
        #endif
#else
        #error Trinity only supports windows!
#endif