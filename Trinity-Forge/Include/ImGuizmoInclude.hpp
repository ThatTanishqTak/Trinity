#pragma once

#include <imgui.h>

#pragma push_macro("IMGUI_API")
#undef IMGUI_API
#define IMGUI_API
#include <ImGuizmo.h>
#pragma pop_macro("IMGUI_API")

void ReleaseImGuizmo();