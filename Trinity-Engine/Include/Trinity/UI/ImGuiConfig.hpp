#pragma once

// Dear ImGui's configuration, read through IMGUI_USER_CONFIG by every file that includes imgui.h, inside the engine and out
#include "Trinity/Core/Export.hpp"

#define IMGUI_API TRINITY_API

#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_ENABLE_FREETYPE

// Characters above U+FFFF, such as typed emoji and Nerd Font icons, need a 32-bit ImWchar
#define IMGUI_USE_WCHAR32

// ImTextureID is a texture's bindless index, so 0 is a valid one
#define ImTextureID_Invalid (~static_cast<ImTextureID>(0))

// Files reach ImGui only from memory, through the virtual file system
#define IMGUI_DISABLE_FILE_FUNCTIONS