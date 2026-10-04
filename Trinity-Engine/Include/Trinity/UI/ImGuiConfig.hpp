#pragma once

// Dear ImGui's configuration, read through IMGUI_USER_CONFIG by every file that includes imgui.h, inside the engine and out
#include "Trinity/Core/Export.hpp"

#define IMGUI_API TRINITY_API

#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_ENABLE_FREETYPE

// Files reach ImGui only from memory, through the virtual file system
#define IMGUI_DISABLE_FILE_FUNCTIONS