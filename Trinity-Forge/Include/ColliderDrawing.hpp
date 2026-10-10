#pragma once

#include <Trinity.hpp>

#include <vector>

// The colliders of an entity and everything under it, drawn through DebugDraw in world space, with their entity's world scale as the physics world will take it: a box, convex hull or mesh exactly, a sphere by its largest axis, and a capsule or cylinder by its Y axis along its length and its larger other axis around it. Solid colliders are green and triggers blue. After the transform pass, before the frame graph is built
class ColliderDrawing
{
public:
    ColliderDrawing() = default;
    ~ColliderDrawing();

    ColliderDrawing(const ColliderDrawing&) = delete;
    ColliderDrawing& operator=(const ColliderDrawing&) = delete;

    // A hull or mesh collider draws once its shape has loaded, which drawing it starts. A shape is held while a collider using it is drawn, and let go the first time none is
    void Draw(Trinity::Scene& scene, Trinity::Entity root);
    // Every shape held is let go, as when nothing is drawn or the project closes
    void Release();

private:
    std::vector<Trinity::UUID> m_Held;
    std::vector<Trinity::UUID> m_Drawn;
};