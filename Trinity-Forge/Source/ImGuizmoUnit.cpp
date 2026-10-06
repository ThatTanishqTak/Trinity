// ImGuizmo, compiled into Forge against the engine's Dear ImGui. The engine's imconfig makes IMGUI_API mean TRINITY_API, which would mark ImGuizmo's own functions as the engine's, so ImGuizmoInclude.hpp declares them first without it. ImGuizmo.cpp uses ImGui's vector operators, which must be on before imgui.h is first read
#define IMGUI_DEFINE_MATH_OPERATORS
#include "ImGuizmoInclude.hpp"

#include <ImGuizmo.cpp>

// ImGuizmo keeps its state in a static context, whose ID stack is allocated through ImGui's allocator on first use and would otherwise only be freed after the engine's memory has been checked for leaks
void ReleaseImGuizmo()
{
    ImGuizmo::gContext.mIDStack.clear();
}