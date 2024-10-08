#pragma once

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

#include "Trinity/Core/Log.h"
#include "Trinity/Debug/Instrumentor.h"

// PLATFORM DEPENDENT
#ifdef TR_PLATFORM_WINDOWS
    #include <Windows.h>
#endif