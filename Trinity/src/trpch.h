#pragma once

#include "Trinity/Core/PlatformDetection.h"

#ifdef TR_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

// PLATFORM INDEPENDENT HEADERS
#include <iostream>
#include <memory>
#include <utility>
#include <functional>
#include <algorithm>

// DATA STRUCTURES
#include <string>
#include <sstream>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>

#include "Trinity/Core/Core.h"
#include "Trinity/Core/Log.h"
#include "Trinity/Debug/Instrumentor.h"

// PLATFORM DEPENDENT
#ifdef TR_PLATFORM_WINDOWS
    #include <Windows.h>
#endif