// imconfig makes IMGUI_API mean TRINITY_API, so ImGuizmoInclude.hpp declares ImGuizmo's first
#define IMGUI_DEFINE_MATH_OPERATORS
#include "ImGuizmoInclude.hpp"

#include <ImGuizmo.cpp>
#include <ImCurveEdit.cpp>

// ImGuizmo's static context allocates through ImGui on first use. Freed here for the leak check
void ReleaseImGuizmo()
{
    ImGuizmo::gContext.mIDStack.clear();
}