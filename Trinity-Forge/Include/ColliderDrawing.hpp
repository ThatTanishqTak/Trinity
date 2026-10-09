#pragma once

#include <Trinity.hpp>

// The colliders of an entity and everything under it, drawn through DebugDraw in world space, with their entity's world scale as the physics world will take it: a box exactly, a sphere by its largest axis, and a capsule or cylinder by its Y axis along its length and its larger other axis around it. Solid colliders are green and triggers blue. After the transform pass, before the frame graph is built
void DrawColliders(Trinity::Scene& scene, Trinity::Entity root);